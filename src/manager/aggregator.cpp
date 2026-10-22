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
#include <limits>

namespace SpDiag {

void Aggregator::Aggregate(
    const ProgramEntry* programs, uint32_t numPrograms,
    const char (*keyPointNames)[nameLen], const char (*keyModuleNames)[nameLen],
    const ShardBuffer& srcShards, uint32_t numCores,
    const GlobalPerfStat* globalStats, uint32_t globalCount)
{
    std::map<uint32_t, AggregatedStat> statMap;
    const size_t coreCount = std::min(static_cast<size_t>(numCores), srcShards.size());
    const uint32_t maxSlots = srcShards.maxSlots();
#ifdef SPDIAG_ENABLE_PERCENTILE
    std::vector<uint64_t> percentileCandidates;
    size_t perCoreCandidates = 0;
    size_t candidateCapacity = 0;
    if (CheckedSizeMultiply(coreCount, srcShards.perCoreTopNCapacity(),
                            perCoreCandidates) &&
        CheckedSizeAdd(perCoreCandidates, maxTopN, candidateCapacity)) {
        percentileCandidates.reserve(candidateCapacity);
    }
#endif

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

                entry.level = stat.level;
            }
        }
    }

    // 全局 PerfPoint 累加区：按 globalSlotIndex 直接合并到同一 statMap（与 local 同 key 汇总）。
    const uint32_t boundedGlobalCount =
        (globalStats == nullptr) ? 0 : std::min(globalCount, maxSlots);
    if (globalStats != nullptr && globalCount > 0) {
        for (uint32_t gi = 0; gi < boundedGlobalCount; ++gi) {
            const PerfStat& stat = globalStats[gi].stat;
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
        }
    }

    for (auto& [slotIdx, entry] : statMap) {
        if (entry.totalTickCount > 0) {
            entry.avgTimeNs = static_cast<double>(entry.totalTimeNs) / static_cast<double>(entry.totalTickCount);
        }

#ifdef SPDIAG_ENABLE_PERCENTILE
        percentileCandidates.clear();
        uint64_t truncationFloor = 0;
        bool coverageUnknown = false;
        for (size_t core = 0; core < coreCount; ++core) {
            const PerfStat& stat = srcShards.stats(core)[slotIdx];
            if (stat.tickCount == 0 || stat.slotIndex != slotIdx) {
                continue;
            }
            const auto topN = srcShards.topN(core, slotIdx);
            uint64_t sourceFloor = std::numeric_limits<uint64_t>::max();
            for (uint32_t t = 0; t < topN.count; ++t) {
                if (topN.values[t] > 0) {
                    percentileCandidates.push_back(topN.values[t]);
                    sourceFloor = std::min(sourceFloor, topN.values[t]);
                }
            }
            if (stat.tickCount > topN.count) {
                if (sourceFloor == std::numeric_limits<uint64_t>::max()) {
                    coverageUnknown = true;
                } else {
                    truncationFloor = std::max(truncationFloor, sourceFloor);
                }
            }
        }
        if (slotIdx < boundedGlobalCount) {
            const PerfStat& stat = globalStats[slotIdx].stat;
            const uint32_t count = std::min<uint32_t>(stat.topNCount, maxTopN);
            uint64_t sourceFloor = std::numeric_limits<uint64_t>::max();
            for (uint32_t t = 0; t < count; ++t) {
                if (globalStats[slotIdx].topN[t] > 0) {
                    percentileCandidates.push_back(
                        globalStats[slotIdx].topN[t]);
                    sourceFloor = std::min(
                        sourceFloor, globalStats[slotIdx].topN[t]);
                }
            }
            if (stat.tickCount > count) {
                if (sourceFloor == std::numeric_limits<uint64_t>::max()) {
                    coverageUnknown = true;
                } else {
                    truncationFloor = std::max(truncationFloor, sourceFloor);
                }
            }
        }
        CalculatePercentilesUnsorted(percentileCandidates,
            entry.totalTickCount,
            entry.p99Ns, entry.hasP99,
            entry.p999Ns, entry.hasP999,
            entry.p9999Ns, entry.hasP9999,
            truncationFloor, coverageUnknown);
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

}

const std::vector<AggregatedStat>& Aggregator::Results() const {
    return results_;
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
