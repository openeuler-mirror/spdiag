/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
 * SpDiag is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *         http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "perf_timer.h"
#include "shm_writer.h"
#include "spdiag/perf_shard.h"
#include "spdiag/shm_layout.h"
#include "spdiag/perf_key_defined.h"
#include "spdiag/log_macros.h"
#include "platform.h"
#include "global_perf_point_factory.h"
#include <chrono>
#include <random>
#include <algorithm>
#include <cstring>
#include <new>

namespace SpDiag {

namespace {
constexpr auto RLX = std::memory_order_relaxed;
constexpr auto requestPollInterval = std::chrono::milliseconds(50);
}

PerfTimer::PerfTimer(ShmWriter& writer, PerfShardManager& shardManager, uint32_t intervalMs,
                     uint32_t baseOffset, uint32_t slotCount, uint32_t programIndex)
    : writer_(writer)
    , shardManager_(shardManager)
    , intervalMs_(intervalMs)
    , baseOffset_(baseOffset)
    , slotCount_(slotCount)
    , programIndex_(programIndex)
{
    if (const ShmHeader* header = writer_.Header()) {
        observedDumpRequest_ = header->dumpRequestGeneration.load(
            std::memory_order_acquire);
        // ConnectAndRegister aligns the local shard state before constructing
        // the timer.  Starting from that local generation ensures a clear
        // published immediately afterwards is still observed and ACKed.
        generation_ = shardManager_.GetClearGeneration();
    }
}

PerfTimer::~PerfTimer() {
    Stop();
}

void PerfTimer::Start() {
    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&PerfTimer::DumpLoop, this);
}

void PerfTimer::Stop() {
    if (!running_.load(std::memory_order_acquire)) {
        if (thread_.joinable()) {
            thread_.join();
        }
        return;
    }
    running_.store(false, std::memory_order_release);
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
    (void)DoOneDump();
}

void PerfTimer::SetInterval(uint32_t intervalMs) {
    intervalMs_ = intervalMs;
}

void PerfTimer::TriggerDumpNow() {
    localDumpRequested_.store(true, std::memory_order_release);
    cv_.notify_all();
}

void PerfTimer::DumpLoop() {
    try {
        auto nextPeriodic = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(intervalMs_);
        while (running_.load(std::memory_order_acquire)) {
            std::unique_lock<std::mutex> lock(cvMutex_);
            cv_.wait_for(lock, requestPollInterval, [this]() {
                return !running_.load(std::memory_order_acquire) ||
                       localDumpRequested_.load(std::memory_order_acquire);
            });
            lock.unlock();

            if (!running_.load(std::memory_order_acquire)) {
                break;
            }
            ShmHeader* header = writer_.Header();
            if (header == nullptr) continue;
            if (ClearRequested(header)) {
                AcknowledgeClear(generation_);
                continue;
            }
            const uint64_t request = header->dumpRequestGeneration.load(
                std::memory_order_acquire);
            const auto now = std::chrono::steady_clock::now();
            const bool requested = request != observedDumpRequest_;
            const bool local = localDumpRequested_.exchange(
                false, std::memory_order_acq_rel);
            const bool periodic = intervalMs_ > 0 && now >= nextPeriodic;
            if ((requested || local || periodic) && DoOneDump()) {
                if (requested) {
                    observedDumpRequest_ = request;
                    Acknowledge(request);
                }
                if (periodic) {
                    nextPeriodic = now + std::chrono::milliseconds(intervalMs_);
                }
            }
        }
    } catch (const std::exception& e) {
        SPDIAG_LOG_ERROR(LogModule::SDK, "PerfTimer DumpLoop exception: %s", e.what());
        WithdrawRegistration();
    } catch (...) {
        SPDIAG_LOG_ERROR(LogModule::SDK, "PerfTimer DumpLoop unknown exception");
        WithdrawRegistration();
    }
}

bool PerfTimer::DoOneDump() {
    ShmHeader* hdr = writer_.Header();
    if (!hdr) {
        return false;
    }
    if (ClearRequested(hdr)) {
        AcknowledgeClear(generation_);
        return false;
    }

    const uint32_t numCores = shardManager_.GetNumCores();
    LocalPerfShard* srcShards = shardManager_.GetShardArray();

    try {
        if (snapshotStatsSize_ != slotCount_) {
            std::unique_ptr<PerfStat[]> stats(new PerfStat[slotCount_]);
            snapshotStats_ = std::move(stats);
            snapshotStatsSize_ = slotCount_;
        }
    } catch (const std::bad_alloc&) {
        SPDIAG_LOG_ERROR(LogModule::SDK,
                         "failed to allocate PerfStat dump storage");
        return false;
    }

#ifdef SPDIAG_ENABLE_PERCENTILE
    const uint32_t perCoreTopNCapacity =
        shardManager_.GetPerCoreTopNCapacity();
    size_t perCoreValues = 0;
    size_t requiredValues = 0;
    size_t requiredCounts = 0;
    if (!CheckedSizeMultiply(slotCount_, perCoreTopNCapacity,
                             perCoreValues) ||
        !CheckedSizeMultiply(numCores, perCoreValues, requiredValues) ||
        !CheckedSizeMultiply(numCores, slotCount_, requiredCounts)) {
        SPDIAG_LOG_ERROR(LogModule::SDK,
                         "Top-N snapshot size overflow");
        return false;
    }
    try {
        if (snapshotTopNSize_ != requiredValues ||
            snapshotTopNCountSize_ != requiredCounts) {
            std::unique_ptr<uint64_t[]> values(
                new uint64_t[requiredValues]);
            std::unique_ptr<uint16_t[]> counts(
                new uint16_t[requiredCounts]);
            snapshotTopN_ = std::move(values);
            snapshotTopNCounts_ = std::move(counts);
            snapshotTopNSize_ = requiredValues;
            snapshotTopNCountSize_ = requiredCounts;
        }
    } catch (const std::bad_alloc&) {
        SPDIAG_LOG_ERROR(LogModule::SDK,
                         "failed to allocate Top-N dump storage");
        return false;
    }

    // Atomic loads happen before the shared-memory writer window. The
    // cumulative heap is never cleared, so a concurrent update that lands
    // after this copy is naturally visible in the next dump.
    bool topNStable = true;
    for (uint32_t core = 0; core < numCores; ++core) {
        topNStable = shardManager_.SnapshotTopN(
            snapshotTopN_.get() + static_cast<size_t>(core) * perCoreValues,
            snapshotTopNCounts_.get() +
                static_cast<size_t>(core) * slotCount_,
            srcShards[core]) && topNStable;
    }
    if (!topNStable) {
        return false;
    }
#endif

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 50);

    // Keep the wait bounded for a writer that died with an odd sequence, but
    // allow all registered writers to serialize a large-K dump without
    // discarding their already-built snapshots and immediately rebuilding.
    auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(DumpWindowTimeoutMs(
            hdr->perCoreTopNCapacity, hdr->registeredPrograms));
    while (true) {
        uint64_t expected = hdr->dumpSeqNo.load(std::memory_order_acquire);
        if (expected % 2 != 0) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(dist(gen)));
            continue;
        }
        if (hdr->dumpSeqNo.compare_exchange_weak(
                expected, expected + 1,
                std::memory_order_acq_rel)) {
            break;
        }
    }

    if (ClearRequested(hdr)) {
        AcknowledgeClear(generation_);
        hdr->dumpSeqNo.fetch_add(1, std::memory_order_release);
        return false;
    }

    uint32_t shmMaxSlots = hdr->maxSlots;

    for (uint32_t i = 0; i < numCores; ++i) {
        shardManager_.SnapshotStatsAndReset(snapshotStats_.get(),
                                            srcShards[i]);
#ifdef SPDIAG_ENABLE_PERCENTILE
        const size_t countOffset = static_cast<size_t>(i) * slotCount_;
        for (uint32_t slot = 0; slot < slotCount_; ++slot) {
            snapshotStats_[slot].topNCount =
                snapshotTopNCounts_[countOffset + slot];
        }
        const uint64_t* topNValues = snapshotTopN_.get() +
            static_cast<size_t>(i) * perCoreValues;
#else
        const uint64_t* topNValues = nullptr;
#endif

        void* shmShardPtr = writer_.ShardPtr(programIndex_, i);
        MergeShard(shmShardPtr, shmMaxSlots, snapshotStats_.get(),
                   topNValues);
    }

    // 全局 PerfPoint 累加区快照+合并（仍在 dumpSeqNo 持锁期内）。
    SnapshotAndMergeGlobal();

    hdr->lastDumpTimestampNs = Platform::GetTimestampNs();
    std::atomic_thread_fence(std::memory_order_release);
    hdr->dumpSeqNo.fetch_add(1, std::memory_order_release);

    writer_.WriteHeader(*hdr);
    return true;
}

void PerfTimer::Acknowledge(uint64_t requestGeneration) {
    ShmHeader* header = writer_.Header();
    if (header == nullptr || programIndex_ >= header->registeredPrograms) return;
    auto* registry = reinterpret_cast<ProgramEntry*>(
        reinterpret_cast<uint8_t*>(header) + header->registryOffset);
    __atomic_store_n(&registry[programIndex_].dumpAckGeneration,
                     requestGeneration, __ATOMIC_RELEASE);
}

void PerfTimer::AcknowledgeClear(uint64_t requestGeneration) {
    ShmHeader* header = writer_.Header();
    if (header == nullptr || programIndex_ >= header->registeredPrograms) return;
    auto* registry = reinterpret_cast<ProgramEntry*>(
        reinterpret_cast<uint8_t*>(header) + header->registryOffset);
    __atomic_store_n(&registry[programIndex_].clearAckGeneration,
                     requestGeneration, __ATOMIC_RELEASE);
}

void PerfTimer::WithdrawRegistration() {
    ShmHeader* header = writer_.Header();
    if (header == nullptr || programIndex_ >= header->registeredPrograms) return;
    auto* registry = reinterpret_cast<ProgramEntry*>(
        reinterpret_cast<uint8_t*>(header) + header->registryOffset);
    __atomic_store_n(&registry[programIndex_].pid, 0, __ATOMIC_RELEASE);
}

bool PerfTimer::ClearRequested(ShmHeader* header) {
    const uint32_t request =
        __atomic_load_n(&header->generation, __ATOMIC_ACQUIRE);
    if (request == generation_) {
        return false;
    }
    GlobalPerfPointFactory::Instance().ClearPending();
    shardManager_.SetClearGeneration(request);
    generation_ = request;
    return true;
}

void PerfTimer::SnapshotAndMergeGlobal() {
    // 必须在 dumpSeqNo 持锁期内调用（由 DoOneDump 保证），维持单 writer 协议。
    GlobalPerfStat* shmGlobal = writer_.GlobalAccumPtr();
    if (shmGlobal == nullptr) {
        return;
    }
    auto& gfac = GlobalPerfPointFactory::Instance();

    GlobalAccum* accums = gfac.Array();
    uint32_t count = gfac.Count();
    uint32_t shmCount = writer_.Header()->globalAccumCount;
    uint32_t n = (count < shmCount) ? count : shmCount;   // 双侧取小，防越界

    for (uint32_t g = 0; g < n; ++g) {
        GlobalAccum& a = accums[g];
        const uint64_t tick = LockGlobalAccum(a);
        // 独占窗口内拿走本轮完整增量；后续 End 落到下一轮。
        uint64_t good = a.goodCount.exchange(0, RLX);
        uint64_t bad  = a.badCount.exchange(0, RLX);
        uint64_t notc = a.notCount.exchange(0, RLX);
        uint64_t time = a.totalTimeNs.exchange(0, RLX);
        uint64_t tmin = a.minTimeNs.exchange(0, RLX);
        uint64_t tmax = a.maxTimeNs.exchange(0, RLX);
        uint32_t level = a.level.load(RLX);
#ifdef SPDIAG_ENABLE_PERCENTILE
        uint64_t pendingTopN[maxTopN];
        uint32_t limit = std::min<uint32_t>(a.topNCount.exchange(0, RLX),
                                            maxTopN);
        std::copy_n(a.topN, limit, pendingTopN);
#endif
        UnlockGlobalAccum(a);
        if (tick == 0 && notc == 0) {
            continue;
        }

        PerfStat& dst = shmGlobal[g].stat;
        dst.slotIndex      = g;
        dst.backGoodCount += static_cast<uint32_t>(good);
        dst.backBadCount  += static_cast<uint32_t>(bad);
        dst.backNotCount  += static_cast<uint32_t>(notc);
        dst.tickCount     += static_cast<uint32_t>(tick);
        dst.totalTimeNs   += time;
        if (tmin > 0 && (dst.minTimeNs == 0 || tmin < dst.minTimeNs)) {
            dst.minTimeNs = tmin;
        }
        if (tmax > dst.maxTimeNs) {
            dst.maxTimeNs = tmax;
        }
        dst.level = static_cast<uint8_t>(level);
#ifdef SPDIAG_ENABLE_PERCENTILE
        // 转录全局 topN 到 SHM PerfStat
        for (uint32_t k = 0; k < limit; ++k) {
            InsertTopN({shmGlobal[g].topN, &dst.topNCount, maxTopN},
                       pendingTopN[k]);
        }
#endif
    }
}


void PerfTimer::MergeShard(
    void* dst, uint32_t dstMaxSlots, const PerfStat* srcStats,
    const uint64_t* snapshotTopN) {
#ifndef SPDIAG_ENABLE_PERCENTILE
    (void)snapshotTopN;
#endif
    PerfStat* dstStats = ShmShardStats(dst);
    for (uint32_t j = 0; j < slotCount_; ++j) {
        const PerfStat& srcStat = srcStats[j];
#ifdef SPDIAG_ENABLE_PERCENTILE
        const bool hasTopN = srcStat.topNCount > 0;
#else
        const bool hasTopN = false;
#endif
        if (srcStat.tickCount == 0 &&
            srcStat.backNotCount == 0 && !hasTopN) {
            continue;
        }

        uint32_t globalIdx = baseOffset_ + j;
        if (globalIdx >= dstMaxSlots) {
            continue;
        }

        PerfStat& dstStat = dstStats[globalIdx];
        dstStat.slotIndex = globalIdx;
        if (srcStat.level != 0) {
            dstStat.level = srcStat.level;
        }
        dstStat.backGoodCount += srcStat.backGoodCount;
        dstStat.backBadCount += srcStat.backBadCount;
        dstStat.backNotCount += srcStat.backNotCount;
        dstStat.tickCount += srcStat.tickCount;
        dstStat.totalTimeNs += srcStat.totalTimeNs;

        if (srcStat.minTimeNs > 0 &&
            (dstStat.minTimeNs == 0 || srcStat.minTimeNs < dstStat.minTimeNs)) {
            dstStat.minTimeNs = srcStat.minTimeNs;
        }
        if (srcStat.maxTimeNs > dstStat.maxTimeNs) {
            dstStat.maxTimeNs = srcStat.maxTimeNs;
        }

#ifdef SPDIAG_ENABLE_PERCENTILE
        const uint32_t capacity = shardManager_.GetPerCoreTopNCapacity();
        const uint64_t* sourceValues = snapshotTopN +
            static_cast<size_t>(j) * capacity;
        const uint32_t sourceCount = std::min<uint32_t>(
            srcStat.topNCount, capacity);
        TopNView destination = ShmShardTopN(dst, globalIdx, dstMaxSlots,
                                            capacity);
        std::copy_n(sourceValues, sourceCount, destination.values);
        *destination.count = static_cast<uint16_t>(sourceCount);
#endif
#ifdef SPDIAG_ENABLE_PERFLOG
        if (srcStat.perflogWritePos.load(std::memory_order_relaxed) > 0) {
            struct PlEntry { uint64_t ns; uint64_t cost; uint16_t tid; };
            PlEntry merged[200];
            uint32_t mergedCount = 0;
            uint32_t dstCount = dstStat.perflogWritePos.load(std::memory_order_relaxed);
            if (dstCount > perflogTimestampCount) dstCount = perflogTimestampCount;
            for (uint32_t t = 0; t < dstCount; ++t) {
                if (dstStat.timestamps[t] != 0) {
                    merged[mergedCount].ns = dstStat.timestamps[t];
                    merged[mergedCount].cost = dstStat.perflogCosts[t];
                    merged[mergedCount].tid = dstStat.perflogThreadIds[t];
                    mergedCount++;
                }
            }
            uint32_t srcCount = srcStat.perflogWritePos.load(std::memory_order_relaxed);
            if (srcCount > perflogTimestampCount) srcCount = perflogTimestampCount;
            for (uint32_t t = 0; t < srcCount; ++t) {
                if (srcStat.timestamps[t] != 0) {
                    merged[mergedCount].ns = srcStat.timestamps[t];
                    merged[mergedCount].cost = srcStat.perflogCosts[t];
                    merged[mergedCount].tid = srcStat.perflogThreadIds[t];
                    mergedCount++;
                }
            }
            if (mergedCount > 0) {
                std::sort(merged, merged + mergedCount,
                    [](const PlEntry& a, const PlEntry& b) { return a.ns < b.ns; });
                uint32_t keep = (mergedCount > perflogTimestampCount) ? perflogTimestampCount : mergedCount;
                const PlEntry* base = merged + mergedCount - keep;
                for (uint32_t k = 0; k < keep; ++k) {
                    dstStat.timestamps[k] = base[k].ns;
                    dstStat.perflogCosts[k] = base[k].cost;
                    dstStat.perflogThreadIds[k] = base[k].tid;
                }
                for (uint32_t k = keep; k < perflogTimestampCount; ++k) {
                    dstStat.timestamps[k] = 0;
                    dstStat.perflogCosts[k] = 0;
                    dstStat.perflogThreadIds[k] = 0;
                }
                dstStat.perflogWritePos.store(keep, std::memory_order_relaxed);
            }
        }
#endif
    }
}

}
