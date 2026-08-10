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

#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include "spdiag/shm_layout.h"

namespace SpDiag {

struct AggregatedStat {
    uint32_t    globalSlotIndex = 0;
    std::string programName;
    std::string moduleName;
    std::string pointName;
    uint8_t     level = 0;

    uint64_t    totalGoodCount = 0;
    uint64_t    totalBadCount = 0;
    uint64_t    totalNotCount = 0;
    uint64_t    totalTickCount = 0;

    uint64_t    totalTimeNs = 0;
    uint64_t    globalMinTimeNs = 0;
    uint64_t    globalMaxTimeNs = 0;
    double      avgTimeNs = 0.0;

#ifdef SPDIAG_ENABLE_PERCENTILE
    std::vector<uint64_t> globalTopN;

    uint64_t    p99Ns = 0;
    uint64_t    p999Ns = 0;
    uint64_t    p9999Ns = 0;
    bool        hasP99 = false;
    bool        hasP999 = false;
    bool        hasP9999 = false;
#endif
};

inline void MergeStat(AggregatedStat& target, const AggregatedStat& delta) {
    target.totalGoodCount += delta.totalGoodCount;
    target.totalBadCount += delta.totalBadCount;
    target.totalNotCount += delta.totalNotCount;
    target.totalTickCount += delta.totalTickCount;
    target.totalTimeNs += delta.totalTimeNs;

    if (delta.globalMinTimeNs > 0) {
        if (target.globalMinTimeNs == 0 || delta.globalMinTimeNs < target.globalMinTimeNs) {
            target.globalMinTimeNs = delta.globalMinTimeNs;
        }
    }
    if (delta.globalMaxTimeNs > target.globalMaxTimeNs) {
        target.globalMaxTimeNs = delta.globalMaxTimeNs;
    }

#ifdef SPDIAG_ENABLE_PERCENTILE
    target.globalTopN.insert(target.globalTopN.end(), delta.globalTopN.begin(), delta.globalTopN.end());
    std::sort(target.globalTopN.begin(), target.globalTopN.end(), std::greater<uint64_t>());
    if (target.globalTopN.size() > maxTopN) {
        target.globalTopN.resize(maxTopN);
    }

    target.p99Ns = delta.p99Ns;
    target.p999Ns = delta.p999Ns;
    target.p9999Ns = delta.p9999Ns;
    target.hasP99 = delta.hasP99;
    target.hasP999 = delta.hasP999;
    target.hasP9999 = delta.hasP9999;
#endif
}

inline void RecalcAvg(AggregatedStat& stat) {
    if (stat.totalTickCount > 0) {
        stat.avgTimeNs = static_cast<double>(stat.totalTimeNs) / static_cast<double>(stat.totalTickCount);
    }
}

class Aggregator {
public:
    void Aggregate(const ProgramEntry* programs, uint32_t numPrograms,
                   const char (*keyPointNames)[nameLen], const char (*keyModuleNames)[nameLen],
                   const ShardBuffer& srcShards, uint32_t numCores,
                   const PerfStat* globalStats = nullptr, uint32_t globalCount = 0);
    void MergeDelta(const std::vector<AggregatedStat>& deltaStats);

    const std::vector<AggregatedStat>& Results() const;
    const ShardBuffer& PerCoreShards() const;

private:
    std::vector<AggregatedStat> results_;
    ShardBuffer shards_;
};

}
