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

#include "spdiag/perf_shard.h"
#include "spdiag/perf_key_defined.h"
#include "platform.h"
#include <cstring>
#include <algorithm>
#include <new>

namespace SpDiag {

bool PerfShardManager::Init(uint32_t numCores, uint32_t slotCount,
                            uint32_t perCoreTopNCapacity) {
    if (numCores == 0 || slotCount == 0 || slotCount > MAX_PERF_SLOTS ||
        perCoreTopNCapacity < defaultPerCoreTopN ||
        perCoreTopNCapacity > maxPerCoreTopN) {
        return false;
    }
    numCores_ = numCores;
    slotCount_ = slotCount;
    perCoreTopNCapacity_ = perCoreTopNCapacity;
    try {
        shards_ = std::make_unique<LocalPerfShard[]>(numCores);
    } catch (const std::bad_alloc&) {
        numCores_ = 0;
        slotCount_ = 0;
        return false;
    }
    for (uint32_t i = 0; i < numCores; ++i) {
        shards_[i].coreId = i;
        for (uint32_t j = 0; j < MAX_PERF_SLOTS; ++j) {
            shards_[i].stats[j].reset();
        }
    }
#ifdef SPDIAG_ENABLE_PERCENTILE
    size_t stateCount = 0;
    size_t valueCount = 0;
    if (!CheckedSizeMultiply(numCores_, slotCount_, stateCount) ||
        !CheckedSizeMultiply(stateCount, perCoreTopNCapacity_,
                             valueCount)) {
        shards_.reset();
        numCores_ = 0;
        slotCount_ = 0;
        return false;
    }
    try {
        perCoreTopNStorage_.values =
            std::make_unique<std::atomic<uint64_t>[]>(valueCount);
        perCoreTopNStorage_.counts =
            std::make_unique<std::atomic<uint16_t>[]>(stateCount);
        for (size_t i = 0; i < stateCount; ++i) {
            std::atomic_init(perCoreTopNStorage_.counts.get() + i,
                             static_cast<uint16_t>(0));
        }
        for (uint32_t core = 0; core < numCores_; ++core) {
            for (uint32_t slot = 0; slot < slotCount_; ++slot) {
                const size_t stateIndex =
                    static_cast<size_t>(core) * slotCount_ + slot;
                shards_[core].perCoreTopNValues[slot] =
                    perCoreTopNStorage_.values.get() +
                    stateIndex * perCoreTopNCapacity_;
                shards_[core].perCoreTopNCounts[slot] =
                    perCoreTopNStorage_.counts.get() + stateIndex;
            }
        }
    } catch (const std::bad_alloc&) {
        shards_.reset();
        perCoreTopNStorage_.values.reset();
        perCoreTopNStorage_.counts.reset();
        numCores_ = 0;
        slotCount_ = 0;
        return false;
    }
#endif
    return true;
}

LocalPerfShard& PerfShardManager::GetShard() {
    return shards_[Platform::GetCurrentCpuId() % numCores_];
}

LocalPerfShard* PerfShardManager::GetShardArray() {
    return shards_.get();
}

void PerfShardManager::Clear() {
    for (uint32_t core = 0; core < numCores_; ++core) {
        shards_[core].reset();
        shards_[core].coreId = core;
    }
}

#ifdef SPDIAG_ENABLE_PERCENTILE
void PerfShardManager::RestoreTopN(LocalPerfShard& shard, uint32_t slot,
                                   ConstTopNView source) {
    auto destination = GetTopN(shard, slot);
    if (destination.count == nullptr) {
        return;
    }
    destination.count->store(0, std::memory_order_relaxed);
    const uint32_t count = std::min(source.count, destination.capacity);
    for (uint32_t i = 0; i < count; ++i) {
        InsertTopNUnchecked(destination, source.values[i]);
    }
}

void PerfShardManager::SnapshotTopN(uint64_t* values, uint16_t* counts,
                                    LocalPerfShard& active) {
    if (values == nullptr || counts == nullptr) {
        return;
    }
    for (uint32_t slot = 0; slot < slotCount_; ++slot) {
        const auto source = GetTopN(active, slot);
        const uint32_t count = std::min<uint32_t>(
            source.count == nullptr ? 0 : source.count->load(
                std::memory_order_acquire),
            perCoreTopNCapacity_);
        counts[slot] = static_cast<uint16_t>(count);
        const size_t offset = static_cast<size_t>(slot) *
                              perCoreTopNCapacity_;
        for (uint32_t i = 0; i < count; ++i) {
            values[offset + i] = source.values[i].load(
                std::memory_order_relaxed);
        }
    }
}
#endif

void PerfShardManager::SnapshotStatsAndReset(PerfStat* snapshotStats,
                                             LocalPerfShard& active) {
    if (snapshotStats == nullptr) {
        return;
    }
    for (uint32_t j = 0; j < slotCount_; ++j) {
        PerfStat& src = active.stats[j];
        PerfStat& dst = snapshotStats[j];

        dst.slotIndex = src.slotIndex;
        dst.level = src.level;

        uint32_t goodBefore = src.backGoodCount;
        uint32_t badBefore = src.backBadCount;
        uint32_t notBefore = src.backNotCount;
        uint32_t tickBefore = src.tickCount;
        uint64_t timeBefore = src.totalTimeNs;

        dst.backGoodCount = goodBefore;
        dst.backBadCount = badBefore;
        dst.backNotCount = notBefore;
        dst.tickCount = tickBefore;
        dst.totalTimeNs = timeBefore;
        dst.minTimeNs = src.minTimeNs;
        dst.maxTimeNs = src.maxTimeNs;

#ifdef SPDIAG_ENABLE_PERFLOG
        std::memcpy(dst.timestamps, src.timestamps, sizeof(src.timestamps));
        std::memcpy(dst.perflogCosts, src.perflogCosts, sizeof(src.perflogCosts));
        std::memcpy(dst.perflogThreadIds, src.perflogThreadIds, sizeof(src.perflogThreadIds));
        dst.perflogWritePos.store(src.perflogWritePos.load(std::memory_order_relaxed), std::memory_order_relaxed);
#endif

        src.backGoodCount -= goodBefore;
        src.backBadCount -= badBefore;
        src.backNotCount -= notBefore;
        src.tickCount -= tickBefore;
        src.totalTimeNs -= timeBefore;
        src.minTimeNs = UINT64_MAX;
        src.maxTimeNs = 0;
#ifdef SPDIAG_ENABLE_PERFLOG
        std::memset(src.timestamps, 0, sizeof(src.timestamps));
        std::memset(src.perflogCosts, 0, sizeof(src.perflogCosts));
        std::memset(src.perflogThreadIds, 0, sizeof(src.perflogThreadIds));
        src.perflogWritePos.store(0, std::memory_order_relaxed);
#endif
    }
}

void PerfShardManager::SnapshotAndReset(LocalPerfShard& snapshot,
                                        LocalPerfShard& active) {
    snapshot.coreId = active.coreId;
    SnapshotStatsAndReset(snapshot.stats, active);
#ifdef SPDIAG_ENABLE_PERCENTILE
    std::vector<uint64_t> snapshotTopN(
        static_cast<size_t>(slotCount_) * perCoreTopNCapacity_);
    std::vector<uint16_t> counts(slotCount_);
    SnapshotTopN(snapshotTopN.data(), counts.data(), active);
    for (uint32_t slot = 0; slot < slotCount_; ++slot) {
        snapshot.stats[slot].topNCount = counts[slot];
    }
#endif
}

}
