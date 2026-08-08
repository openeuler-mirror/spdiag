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

#include "aggregator.h"
#ifdef SPDIAG_ENABLE_PERCENTILE
#include "percentile_calculator.h"
#endif
#include <map>
#include <algorithm>
#include <cstring>

namespace SpDiag {

void Aggregator::Aggregate(
    const ProgramEntry* programs, uint32_t numPrograms,
    const char (*keyPointNames)[nameLen], const char (*keyModuleNames)[nameLen],
    const ShardBuffer& srcShards, uint32_t numCores,
    const PerfStat* globalStats, uint32_t globalCount)
{
    shards_.resize(srcShards.size(), srcShards.maxSlots());
    for (size_t i = 0; i < srcShards.size(); ++i) {
        std::memcpy(shards_.shardData(i), srcShards.shardData(i), ShmShardSize(srcShards.maxSlots()));
    }

    std::map<uint32_t, AggregatedStat> statMap;
    const size_t coreCount = std::min(static_cast<size_t>(numCores), srcShards.size());
    const uint32_t maxSlots = srcShards.maxSlots();

    for (uint32_t p = 0; p < numPrograms; ++p) {
        if (!programs[p].active) continue;
        uint32_t base = programs[p].baseSlot;
        uint32_t count = programs[p].slotCount;
        if (base >= maxSlots) continue;
        uint32_t end = base + std::min(count, maxSlots - base);

        for (size_t core = 0; core < coreCount; ++core) {
            const PerfStat* coreStats = srcShards.stats(core);
            for (uint32_t j = base; j < end; ++j) {
                const PerfStat& stat = coreStats[j];
                if (stat.tickCount == 0) continue;

                uint32_t slotIdx = stat.slotIndex;
                if (slotIdx >= maxSlots) continue;
                auto& entry = statMap[slotIdx];
                entry.globalSlotIndex = slotIdx;

                entry.totalGoodCount += stat.backGoodCount;
                entry.totalBadCount += stat.backBadCount;
                entry.totalNotCount += stat.backNotCount;
                entry.totalTickCount += stat.tickCount;
                entry.totalTimeNs += stat.totalTimeNs;

                uint64_t minNs = stat.minTimeNs;
                if (minNs > 0) {
                    if (entry.globalMinTimeNs == 0 || minNs < entry.globalMinTimeNs) {
                        entry.globalMinTimeNs = minNs;
                    }
                }

                if (stat.maxTimeNs > entry.globalMaxTimeNs) {
                    entry.globalMaxTimeNs = stat.maxTimeNs;
                }

#ifdef SPDIAG_ENABLE_PERCENTILE
                for (uint8_t t = 0; t < stat.topNCount && t < maxTopN; ++t) {
                    if (stat.topN[t] > 0) {
                        entry.globalTopN.push_back(stat.topN[t]);
                    }
                }
#endif

                entry.level = stat.level;
            }
        }
    }

    // 全局 PerfPoint 累加区：按 globalSlotIndex 直接合并到同一 statMap（与 local 同 key 汇总）。
    if (globalStats != nullptr && globalCount > 0) {
        uint32_t boundedGlobalCount = std::min(globalCount, maxSlots);
        for (uint32_t gi = 0; gi < boundedGlobalCount; ++gi) {
            const PerfStat& stat = globalStats[gi];
            if (stat.tickCount == 0) {
                continue;
            }
            auto& entry = statMap[gi];
            entry.globalSlotIndex = gi;

            entry.totalGoodCount += stat.backGoodCount;
            entry.totalBadCount += stat.backBadCount;
            entry.totalNotCount += stat.backNotCount;
            entry.totalTickCount += stat.tickCount;
            entry.totalTimeNs += stat.totalTimeNs;

            uint64_t minNs = stat.minTimeNs;
            if (minNs > 0) {
                if (entry.globalMinTimeNs == 0 || minNs < entry.globalMinTimeNs) {
                    entry.globalMinTimeNs = minNs;
                }
            }
            if (stat.maxTimeNs > entry.globalMaxTimeNs) {
                entry.globalMaxTimeNs = stat.maxTimeNs;
            }
            if (stat.level != 0) {
                entry.level = stat.level;
            }
#ifdef SPDIAG_ENABLE_PERCENTILE
            for (uint8_t t = 0; t < stat.topNCount && t < maxTopN; ++t) {
                if (stat.topN[t] > 0) {
                    entry.globalTopN.push_back(stat.topN[t]);
                }
            }
#endif
        }
    }

    for (auto& [slotIdx, entry] : statMap) {
        if (entry.totalTickCount > 0) {
            entry.avgTimeNs = static_cast<double>(entry.totalTimeNs) / static_cast<double>(entry.totalTickCount);
        }

#ifdef SPDIAG_ENABLE_PERCENTILE
        std::sort(entry.globalTopN.begin(), entry.globalTopN.end(), std::greater<uint64_t>());
        if (entry.globalTopN.size() > maxTopN) {
            entry.globalTopN.resize(maxTopN);
        }
#endif

        for (uint32_t p = 0; p < numPrograms; ++p) {
            if (slotIdx >= programs[p].baseSlot &&
                slotIdx - programs[p].baseSlot < programs[p].slotCount) {
                entry.programName = programs[p].name;
                break;
            }
        }

        entry.moduleName = keyModuleNames[slotIdx];
        entry.pointName = keyPointNames[slotIdx];
    }

    results_.clear();
    results_.reserve(statMap.size());
    for (auto& [slotIdx, entry] : statMap) {
        results_.push_back(std::move(entry));
    }

#ifdef SPDIAG_ENABLE_PERCENTILE
    for (auto& stat : results_) {
        CalculatePercentiles(stat.globalTopN, stat.totalTickCount,
            stat.p99Ns, stat.hasP99,
            stat.p999Ns, stat.hasP999,
            stat.p9999Ns, stat.hasP9999);
    }
#endif
}

const std::vector<AggregatedStat>& Aggregator::Results() const {
    return results_;
}

const ShardBuffer& Aggregator::PerCoreShards() const {
    return shards_;
}

void Aggregator::MergeDelta(const std::vector<AggregatedStat>& deltaStats) {
    for (const auto& delta : deltaStats) {
        bool found = false;
        for (auto& existing : results_) {
            if (existing.globalSlotIndex == delta.globalSlotIndex) {
                MergeStat(existing, delta);
                found = true;
                break;
            }
        }
        if (!found) {
            results_.push_back(delta);
        }
    }

    for (auto& stat : results_) {
        RecalcAvg(stat);
    }
}

}
