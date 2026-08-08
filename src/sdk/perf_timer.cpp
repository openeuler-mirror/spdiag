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
#include <iostream>

namespace SpDiag {

namespace {
constexpr auto RLX = std::memory_order_relaxed;
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
    DoOneDump();
}

void PerfTimer::SetInterval(uint32_t intervalMs) {
    intervalMs_ = intervalMs;
}

void PerfTimer::TriggerDumpNow() {
    cv_.notify_all();
}

void PerfTimer::DumpLoop() {
    try {
        while (running_.load(std::memory_order_acquire)) {
            std::unique_lock<std::mutex> lock(cvMutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(intervalMs_),
                         [this]() { return !running_.load(std::memory_order_acquire); });

            if (!running_.load(std::memory_order_acquire)) {
                break;
            }

            DoOneDump();
        }
    } catch (const std::exception& e) {
        SPDIAG_LOG_ERROR(LogModule::SDK, "PerfTimer DumpLoop exception: %s", e.what());
    } catch (...) {
        SPDIAG_LOG_ERROR(LogModule::SDK, "PerfTimer DumpLoop unknown exception");
    }
}

void PerfTimer::DoOneDump() {
    ShmHeader* hdr = writer_.Header();
    if (!hdr) {
        return;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 50);

    // 上一轮进程崩溃可能导致 dumpSeqNo 永久为奇数，新进程自旋到死。
    // 给锁等待设 500ms 硬上限：正常持锁 ~1ms，即使内核调度换出 CPU
    // 也远不到此限；仅在上轮崩溃场景触发，超时后放弃本轮 dump。
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (true) {
        uint64_t expected = hdr->dumpSeqNo.load(std::memory_order_acquire);
        if (expected % 2 != 0) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return;
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

    uint32_t numCores = shardManager_.GetNumCores();
    LocalPerfShard* srcShards = shardManager_.GetShardArray();
    uint32_t shmMaxSlots = hdr->maxSlots;

    for (uint32_t i = 0; i < numCores; ++i) {
        LocalPerfShard localSnapshot;
        shardManager_.SnapshotAndReset(localSnapshot, srcShards[i]);

        void* shmShardPtr = writer_.ShardPtr(programIndex_, i);
        size_t shardSize = ShmShardSize(shmMaxSlots);
        auto mergedBuf = std::make_unique<uint8_t[]>(shardSize);
        std::memcpy(mergedBuf.get(), shmShardPtr, shardSize);

        MergeShard(mergedBuf.get(), shmMaxSlots, localSnapshot);

        writer_.WriteShard(programIndex_, i, mergedBuf.get(), shardSize);
    }

    // 全局 PerfPoint 累加区快照+合并（仍在 dumpSeqNo 持锁期内）。
    SnapshotAndMergeGlobal();

    hdr->lastDumpTimestampNs = Platform::GetTimestampNs();
    std::atomic_thread_fence(std::memory_order_release);
    hdr->dumpSeqNo.fetch_add(1, std::memory_order_release);

    writer_.WriteHeader(*hdr);
}

void PerfTimer::SnapshotAndMergeGlobal() {
    // 必须在 dumpSeqNo 持锁期内调用（由 DoOneDump 保证），维持单 writer 协议。
    PerfStat* shmGlobal = writer_.GlobalAccumPtr();
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
        // 原子「偷取并清零」：exchange 拿走本轮增量；并发的 End fetch_add 落到下一轮。
        uint64_t good = a.goodCount.exchange(0, RLX);
        uint64_t bad  = a.badCount.exchange(0, RLX);
        uint64_t notc = a.notCount.exchange(0, RLX);
        uint64_t tick = a.tickCount.exchange(0, RLX);
        uint64_t time = a.totalTimeNs.exchange(0, RLX);
        uint64_t tmin = a.minTimeNs.exchange(0, RLX);
        uint64_t tmax = a.maxTimeNs.exchange(0, RLX);
        if (tick == 0 && notc == 0) {
            continue;
        }

        PerfStat& dst = shmGlobal[g];
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
        dst.level = static_cast<uint8_t>(a.level.load(RLX));
#ifdef SPDIAG_ENABLE_PERCENTILE
        // 转录全局 topN 到 SHM PerfStat
        uint32_t tn = a.topNCount.exchange(0, RLX);
        if (tn > 0) {
            uint32_t limit = (tn < maxTopN) ? tn : maxTopN;
            for (uint32_t k = 0; k < limit; ++k) {
                InsertTopN(dst, a.topN[k]);
            }
        }
#endif
    }
}


void PerfTimer::MergeShard(void* dst, uint32_t dstMaxSlots, const LocalPerfShard& src) {
    PerfStat* dstStats = ShmShardStats(dst);
    for (uint32_t j = 0; j < slotCount_; ++j) {
        const PerfStat& srcStat = src.stats[j];
        if (srcStat.tickCount == 0 &&
            srcStat.backNotCount == 0) {
            continue;
        }

        uint32_t globalIdx = baseOffset_ + j;
        if (globalIdx >= dstMaxSlots) {
            continue;
        }

        PerfStat& dstStat = dstStats[globalIdx];
        dstStat.slotIndex = globalIdx;
        dstStat.level = srcStat.level;
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
        for (uint8_t t = 0; t < srcStat.topNCount && t < maxTopN; ++t) {
            InsertTopN(dstStat, srcStat.topN[t]);
        }
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
