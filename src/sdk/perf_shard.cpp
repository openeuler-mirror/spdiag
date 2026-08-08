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

namespace SpDiag {

void PerfShardManager::Init(uint32_t numCores) {
    numCores_ = numCores;
    shards_ = std::make_unique<LocalPerfShard[]>(numCores);
    for (uint32_t i = 0; i < numCores; ++i) {
        shards_[i].coreId = i;
        for (uint32_t j = 0; j < MAX_PERF_SLOTS; ++j) {
            shards_[i].stats[j].reset();
        }
    }
}

LocalPerfShard& PerfShardManager::GetShard() {
    return shards_[Platform::GetCurrentCpuId() % numCores_];
}

LocalPerfShard* PerfShardManager::GetShardArray() {
    return shards_.get();
}

uint32_t PerfShardManager::GetNumCores() const {
    return numCores_;
}

void PerfShardManager::SnapshotAndReset(LocalPerfShard& snapshot, LocalPerfShard& active) {
    snapshot.coreId = active.coreId;
    for (uint32_t j = 0; j < MAX_PERF_SLOTS; ++j) {
        PerfStat& src = active.stats[j];
        PerfStat& dst = snapshot.stats[j];

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

#ifdef SPDIAG_ENABLE_PERCENTILE
        std::copy_n(src.topN, maxTopN, dst.topN);
        dst.topNCount = src.topNCount;
#endif

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

}
