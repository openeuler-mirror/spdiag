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

#include "display_engine.h"
#include "cli_config.h"
#include "sort_spec.h"
#include "sort_engine.h"
#include "glob_match.h"
#include "filter_util.h"
#include "text_table.h"
#include "format_util.h"
#include "memstat_display.h"
#include "mempoint_display.h"
#include "cache_display.h"
#include "frame_filter.h"
#include "spdiag/mem_point_keys.h"
#ifdef SPDIAG_ENABLE_PERCENTILE
#include "manager/percentile_calculator.h"
#endif
#include <iostream>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <functional>
#include <iomanip>

#include <time.h>

namespace SpDiag {

using Cli::FormatComma;
using Cli::FormatSize;
using Cli::FormatHuman;
using Cli::Ellipsize;
using Cli::FormatFirstSeen;
using Cli::FormatRate;
using Cli::ThreeWayCompare;
using Cli::MemColHeader;
using Cli::MemColWidth;
using Cli::ParseShowColumns;
using Cli::ComputeChurn;
using Cli::ParseMemEntries;
using Cli::ShowMemAggregated;
using Cli::ShowMemRaw;
using Cli::ParseMemPointEntries;
using Cli::ShowMemPointDisplay;
using Cli::ShowObCacheDisplay;

static std::string FindProgramName(const ProgramEntry* programs, uint32_t numPrograms, uint32_t slotIdx) {
    if (!programs) return "<unknown>";
    for (uint32_t p = 0; p < numPrograms; ++p) {
        if (slotIdx >= programs[p].baseSlot &&
            slotIdx < programs[p].baseSlot + programs[p].slotCount) {
            return programs[p].name;
        }
    }
    return "<unknown>";
}

std::string DisplayEngine::FormatNs(uint64_t value, int width) const {
    std::string plain = std::to_string(value);
    if (static_cast<int>(plain.size()) <= width - 1) {
        return plain;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2e", static_cast<double>(value));
    return buf;
}

#ifdef SPDIAG_ENABLE_PERCENTILE
std::string DisplayEngine::FormatPercentile(uint64_t value, bool valid, int width) const {
    if (!valid) {
        return std::string(width - 3, ' ') + "N/A";
    }
    return FormatNs(value, width);
}
#endif

void DisplayEngine::ShowRawTable(const ShmInfo& info, const ShardBuffer& shards,
                                 const ProgramEntry* programs, uint32_t numPrograms,
                                 const char (*keyPointNames)[nameLen],
                                 const char (*keyModuleNames)[nameLen],
                                 int32_t coreStart, int32_t coreEnd) {
    int32_t start = (coreStart >= 0) ? coreStart : 0;
    int32_t end = (coreEnd >= 0) ? coreEnd : static_cast<int32_t>(info.numCores) - 1;
    if (end >= static_cast<int32_t>(info.numCores)) {
        end = static_cast<int32_t>(info.numCores) - 1;
    }

    csvWriter_.BeginSession("spdiag_rawtable");

    Cli::TextTable table;
    table.AddColumn("#", 4, true);
    table.AddColumn("Core", 4, true);
    table.AddColumn("Program", 8);
    table.AddColumn("Name", 8);
    table.AddColumn("Lvl", 3, true);
    table.AddColumn("Ticks", 8, true);
    table.AddColumn("Good", 8, true);
    table.AddColumn("Bad", 8, true);
    table.AddColumn("Not", 8, true);
    table.AddColumn("Total(ns)", 14, true);
    table.AddColumn("Avg(ns)", 12, true);
    table.AddColumn("Min(ns)", 12, true);
    table.AddColumn("Max(ns)", 12, true);

    int rowIdx = 1;
    for (int32_t s = start; s <= end; ++s) {
        if (s < 0 || s >= static_cast<int32_t>(shards.size())) continue;
        for (uint32_t i = 0; i < info.maxSlots; ++i) {
            const PerfStat& stat = shards.stats(s)[i];
            uint32_t ticks = stat.tickCount;
            if (ticks == 0) continue;

            uint32_t slotIdx = stat.slotIndex;
            const char* modName = (slotIdx < info.maxSlots && keyModuleNames[slotIdx][0] != '\0')
                                  ? keyModuleNames[slotIdx] : "<unknown>";
            const char* ptName = (slotIdx < info.maxSlots && keyPointNames[slotIdx][0] != '\0')
                                 ? keyPointNames[slotIdx] : "<unknown>";

            uint64_t avgNs = ticks > 0 ? stat.totalTimeNs / ticks : 0;
            std::string name = std::string(modName) + "." + ptName;

            table.AddRow({
                std::to_string(rowIdx),
                std::to_string(s),
                FindProgramName(programs, numPrograms, slotIdx),
                name,
                std::to_string(static_cast<int>(stat.level)),
                std::to_string(ticks),
                std::to_string(stat.backGoodCount),
                std::to_string(stat.backBadCount),
                std::to_string(stat.backNotCount),
                FormatNs(stat.totalTimeNs, 14),
                FormatNs(avgNs, 12),
                FormatNs(stat.minTimeNs, 12),
                FormatNs(stat.maxTimeNs, 12)
            });
            ++rowIdx;
        }
    }
    table.Print();
    std::cout << "\n";
    csvWriter_.EndSession();
}

void DisplayEngine::ShowSummary(const std::vector<AggregatedStat>& stats, const DisplayOptions& opts) {
    std::vector<AggregatedStat> filtered;
    for (const auto& s : stats) {
        if (opts.hasLevelRange) {
            if (s.level < opts.levelRangeStart || s.level > opts.levelRangeEnd) {
                continue;
            }
        }

        if (opts.levelFilter > 0) {
            bool include = false;
            switch (opts.levelOp) {
                case LevelOp::LT:
                    include = s.level < opts.levelFilter;
                    break;
                case LevelOp::LE:
                    include = s.level <= opts.levelFilter;
                    break;
                case LevelOp::EQ:
                    include = s.level == opts.levelFilter;
                    break;
                case LevelOp::GE:
                    include = s.level >= opts.levelFilter;
                    break;
                case LevelOp::GT:
                    include = s.level > opts.levelFilter;
                    break;
            }
            if (!include) continue;
        }
        filtered.push_back(s);
    }

    Cli::ApplyGlobFilter(filtered, opts.filterGlobs, [](const AggregatedStat& s) {
        return s.moduleName + "." + s.pointName;
    });

    // Multi-key sort
    const auto& ppSortKeys = opts.sortKeys.empty()
        ? Cli::DefaultPerfPointSortKeys() : opts.sortKeys;
    Cli::SortAccessorMap<AggregatedStat> perfPointAccessors = {
        { Cli::SortKey::NAME, [](const AggregatedStat& a, const AggregatedStat& b) {
            return (a.moduleName + "." + a.pointName).compare(b.moduleName + "." + b.pointName);
        }},
        { Cli::SortKey::LVL, [](const AggregatedStat& a, const AggregatedStat& b) {
            return ThreeWayCompare(a.level, b.level);
        }},
        { Cli::SortKey::TICKS, [](const AggregatedStat& a, const AggregatedStat& b) {
            return ThreeWayCompare(a.totalTickCount, b.totalTickCount);
        }},
        { Cli::SortKey::GOOD, [](const AggregatedStat& a, const AggregatedStat& b) {
            return ThreeWayCompare(a.totalGoodCount, b.totalGoodCount);
        }},
        { Cli::SortKey::BAD, [](const AggregatedStat& a, const AggregatedStat& b) {
            return ThreeWayCompare(a.totalBadCount, b.totalBadCount);
        }},
        { Cli::SortKey::AVG_TIME, [](const AggregatedStat& a, const AggregatedStat& b) {
            return ThreeWayCompare(a.avgTimeNs, b.avgTimeNs);
        }},
        { Cli::SortKey::MIN_TIME, [](const AggregatedStat& a, const AggregatedStat& b) {
            return ThreeWayCompare(a.globalMinTimeNs, b.globalMinTimeNs);
        }},
        { Cli::SortKey::MAX_TIME, [](const AggregatedStat& a, const AggregatedStat& b) {
            return ThreeWayCompare(a.globalMaxTimeNs, b.globalMaxTimeNs);
        }},
        { Cli::SortKey::TOTAL_TIME, [](const AggregatedStat& a, const AggregatedStat& b) {
            return ThreeWayCompare(a.totalTimeNs, b.totalTimeNs);
        }},
    };
    Cli::ApplyMultiSort(filtered, ppSortKeys, perfPointAccessors);

    csvWriter_.BeginSession("spdiag_perfpoint_summary");

    Cli::TextTable table;
    table.AddColumn("#", 4, true);
    table.AddColumn("Program", 8);
    table.AddColumn("Module", 8);
    table.AddColumn("Point", 8);
    table.AddColumn("Lvl", 3, true);
    table.AddColumn("Ticks", 8, true);
    table.AddColumn("Good", 8, true);
    table.AddColumn("Bad", 8, true);
    table.AddColumn("Not", 8, true);
    table.AddColumn("Total(ns)", 14, true);
    table.AddColumn("Avg(ns)", 12, true);
    table.AddColumn("Min(ns)", 12, true);
    table.AddColumn("Max(ns)", 12, true);
#ifdef SPDIAG_ENABLE_PERCENTILE
    table.AddColumn("P99(ns)", 12, true);
    table.AddColumn("P999(ns)", 12, true);
    table.AddColumn("P9999(ns)", 12, true);
#endif

    int rowIdx = 1;
    for (const auto& s : filtered) {
        std::vector<std::string> cells = {
            std::to_string(rowIdx),
            s.programName,
            s.moduleName,
            s.pointName,
            std::to_string(static_cast<int>(s.level)),
            std::to_string(s.totalTickCount),
            std::to_string(s.totalGoodCount),
            std::to_string(s.totalBadCount),
            std::to_string(s.totalNotCount),
            FormatNs(s.totalTimeNs, 14),
            FormatNs(static_cast<uint64_t>(s.avgTimeNs), 12),
            FormatNs(s.globalMinTimeNs, 12),
            FormatNs(s.globalMaxTimeNs, 12)
        };
#ifdef SPDIAG_ENABLE_PERCENTILE
        cells.push_back(FormatPercentile(s.p99Ns, s.hasP99, 12));
        cells.push_back(FormatPercentile(s.p999Ns, s.hasP999, 12));
        cells.push_back(FormatPercentile(s.p9999Ns, s.hasP9999, 12));
#endif
        table.AddRow(cells);
        ++rowIdx;
    }
    table.Print();
    std::cout << "\n";
    csvWriter_.EndSession();
}

void DisplayEngine::ShowDetail(const ShmInfo& info, const ShardBuffer& shards,
                               const ProgramEntry* programs, uint32_t numPrograms,
                               const char (*keyPointNames)[nameLen],
                               const char (*keyModuleNames)[nameLen],
                               const DisplayOptions& opts) {
    int32_t start = (opts.coreStart >= 0) ? opts.coreStart : 0;
    int32_t end = (opts.coreEnd >= 0) ? opts.coreEnd : static_cast<int32_t>(info.numCores) - 1;
    if (end >= static_cast<int32_t>(info.numCores)) {
        end = static_cast<int32_t>(info.numCores) - 1;
    }

    struct DetailEntry {
        int32_t shardIndex;
        uint32_t statIndex;
        std::string programName;
        std::string moduleName;
        std::string pointName;
        uint64_t avgTimeNs;
        uint64_t totalTimeNs;
        uint32_t tickCount;
        uint32_t goodCount;
        uint32_t badCount;
        uint32_t notCount;
        uint64_t minTimeNs;
        uint64_t maxTimeNs;
        uint8_t level;
#ifdef SPDIAG_ENABLE_PERCENTILE
        uint64_t p99Ns = 0;
        uint64_t p999Ns = 0;
        uint64_t p9999Ns = 0;
        bool hasP99 = false;
        bool hasP999 = false;
        bool hasP9999 = false;
#endif
    };

    std::vector<DetailEntry> entries;

    for (int32_t s = start; s <= end; ++s) {
        if (s < 0 || s >= static_cast<int32_t>(shards.size())) continue;
        for (uint32_t i = 0; i < info.maxSlots; ++i) {
            const PerfStat& stat = shards.stats(s)[i];
            uint32_t ticks = stat.tickCount;
            if (ticks == 0) continue;

            uint32_t slotIdx = stat.slotIndex;
            const char* modName = (slotIdx < info.maxSlots && keyModuleNames[slotIdx][0] != '\0')
                                  ? keyModuleNames[slotIdx] : "<unknown>";
            const char* ptName = (slotIdx < info.maxSlots && keyPointNames[slotIdx][0] != '\0')
                                 ? keyPointNames[slotIdx] : "<unknown>";

            DetailEntry entry;
            entry.shardIndex = s;
            entry.statIndex = i;
            entry.programName = FindProgramName(programs, numPrograms, slotIdx);
            entry.moduleName = modName;
            entry.pointName = ptName;
            entry.avgTimeNs = ticks > 0 ? stat.totalTimeNs / ticks : 0;
            entry.totalTimeNs = stat.totalTimeNs;
            entry.tickCount = ticks;
            entry.goodCount = stat.backGoodCount;
            entry.badCount = stat.backBadCount;
            entry.notCount = stat.backNotCount;
            entry.minTimeNs = stat.minTimeNs;
            entry.maxTimeNs = stat.maxTimeNs;
            entry.level = stat.level;

#ifdef SPDIAG_ENABLE_PERCENTILE
            std::vector<uint64_t> statTopN;
            for (uint8_t t = 0; t < stat.topNCount && t < maxTopN; ++t) {
                if (stat.topN[t] > 0) statTopN.push_back(stat.topN[t]);
            }
            CalculatePercentiles(statTopN, ticks,
                entry.p99Ns, entry.hasP99,
                entry.p999Ns, entry.hasP999,
                entry.p9999Ns, entry.hasP9999);
#endif

            entries.push_back(entry);
        }
    }

    Cli::ApplyGlobFilter(entries, opts.filterGlobs, [](const DetailEntry& e) {
        return e.moduleName + "." + e.pointName;
    });

    const auto& ppSortKeys = opts.sortKeys.empty()
        ? Cli::DefaultPerfPointSortKeys() : opts.sortKeys;
    Cli::SortAccessorMap<DetailEntry> detailAccessors = {
        { Cli::SortKey::NAME, [](const DetailEntry& a, const DetailEntry& b) {
            return (a.moduleName + "." + a.pointName).compare(b.moduleName + "." + b.pointName);
        }},
        { Cli::SortKey::LVL, [](const DetailEntry& a, const DetailEntry& b) {
            return ThreeWayCompare(a.level, b.level);
        }},
        { Cli::SortKey::TICKS, [](const DetailEntry& a, const DetailEntry& b) {
            return ThreeWayCompare(a.tickCount, b.tickCount);
        }},
        { Cli::SortKey::GOOD, [](const DetailEntry& a, const DetailEntry& b) {
            return ThreeWayCompare(a.goodCount, b.goodCount);
        }},
        { Cli::SortKey::BAD, [](const DetailEntry& a, const DetailEntry& b) {
            return ThreeWayCompare(a.badCount, b.badCount);
        }},
        { Cli::SortKey::AVG_TIME, [](const DetailEntry& a, const DetailEntry& b) {
            return ThreeWayCompare(a.avgTimeNs, b.avgTimeNs);
        }},
        { Cli::SortKey::MIN_TIME, [](const DetailEntry& a, const DetailEntry& b) {
            return ThreeWayCompare(a.minTimeNs, b.minTimeNs);
        }},
        { Cli::SortKey::MAX_TIME, [](const DetailEntry& a, const DetailEntry& b) {
            return ThreeWayCompare(a.maxTimeNs, b.maxTimeNs);
        }},
        { Cli::SortKey::TOTAL_TIME, [](const DetailEntry& a, const DetailEntry& b) {
            return ThreeWayCompare(a.totalTimeNs, b.totalTimeNs);
        }},
    };
    Cli::ApplyMultiSort(entries, ppSortKeys, detailAccessors);

    csvWriter_.BeginSession("spdiag_perfpoint_detail");

    Cli::TextTable table;
    table.AddColumn("#", 4, true);
    table.AddColumn("Core", 4, true);
    table.AddColumn("Program", 8);
    table.AddColumn("Name", 8);
    table.AddColumn("Lvl", 3, true);
    table.AddColumn("Ticks", 8, true);
    table.AddColumn("Good", 8, true);
    table.AddColumn("Bad", 8, true);
    table.AddColumn("Not", 8, true);
    table.AddColumn("Total(ns)", 14, true);
    table.AddColumn("Avg(ns)", 12, true);
    table.AddColumn("Min(ns)", 12, true);
    table.AddColumn("Max(ns)", 12, true);
#ifdef SPDIAG_ENABLE_PERCENTILE
    table.AddColumn("P99(ns)", 12, true);
    table.AddColumn("P999(ns)", 12, true);
    table.AddColumn("P9999(ns)", 12, true);
#endif

    int rowIdx = 1;
    for (const auto& entry : entries) {
        std::string name = entry.moduleName + "." + entry.pointName;
        std::vector<std::string> cells = {
            std::to_string(rowIdx),
            std::to_string(entry.shardIndex),
            entry.programName,
            name,
            std::to_string(static_cast<int>(entry.level)),
            std::to_string(entry.tickCount),
            std::to_string(entry.goodCount),
            std::to_string(entry.badCount),
            std::to_string(entry.notCount),
            FormatNs(entry.totalTimeNs, 14),
            FormatNs(entry.avgTimeNs, 12),
            FormatNs(entry.minTimeNs, 12),
            FormatNs(entry.maxTimeNs, 12)
        };
#ifdef SPDIAG_ENABLE_PERCENTILE
        cells.push_back(FormatPercentile(entry.p99Ns, entry.hasP99, 12));
        cells.push_back(FormatPercentile(entry.p999Ns, entry.hasP999, 12));
        cells.push_back(FormatPercentile(entry.p9999Ns, entry.hasP9999, 12));
#endif
        table.AddRow(cells);
        ++rowIdx;
    }
    table.Print();
    std::cout << "\n";
    csvWriter_.EndSession();
}

void DisplayEngine::ShowHistory(const std::vector<DumpRecord>& records, int32_t maxCount) {
    int32_t count = (maxCount >= 0)
        ? std::min(maxCount, static_cast<int32_t>(records.size()))
        : static_cast<int32_t>(records.size());

    csvWriter_.BeginSession("spdiag_history");

    Cli::TextTable table;
    table.AddColumn("#", 4, true);
    table.AddColumn("Timestamp(ns)", 18, true);
    table.AddColumn("Module.Point", 8);
    table.AddColumn("Lvl", 3, true);
    table.AddColumn("DeltaTicks", 10, true);
    table.AddColumn("Avg(ns)", 12, true);
#ifdef SPDIAG_ENABLE_PERCENTILE
    table.AddColumn("P99(ns)", 12, true);
    table.AddColumn("P999(ns)", 12, true);
    table.AddColumn("P9999(ns)", 12, true);
#endif

    int rowIdx = 1;
    for (int32_t r = 0; r < count; ++r) {
        const DumpRecord& rec = records[r];
        for (const auto& st : rec.stats) {
            uint64_t avgNs = st.deltaTickCount > 0 ? st.deltaTimeNs / st.deltaTickCount : 0;
            std::string name = st.moduleName + "." + st.pointName;

            std::vector<std::string> cells = {
                std::to_string(rowIdx),
                std::to_string(rec.timestampNs),
                name,
                std::to_string(static_cast<int>(st.level)),
                std::to_string(st.deltaTickCount),
                FormatNs(avgNs, 12)
            };
#ifdef SPDIAG_ENABLE_PERCENTILE
            cells.push_back(FormatPercentile(st.p99Ns, st.hasP99, 12));
            cells.push_back(FormatPercentile(st.p999Ns, st.hasP999, 12));
            cells.push_back(FormatPercentile(st.p9999Ns, st.hasP9999, 12));
#endif
            table.AddRow(cells);
            ++rowIdx;
        }
    }
    table.Print();
    std::cout << "\n";
    csvWriter_.EndSession();
}

void DisplayEngine::ShowHistoryDetail(const DumpRecord& record) {
    std::cout << "\nDump Record - Timestamp: " << record.timestampNs << "\n";

    csvWriter_.BeginSession("spdiag_history_detail");

    Cli::TextTable table;
    table.AddColumn("#", 4, true);
    table.AddColumn("Module", 8);
    table.AddColumn("Point", 8);
    table.AddColumn("Lvl", 3, true);
    table.AddColumn("Ticks", 8, true);
    table.AddColumn("Good", 8, true);
    table.AddColumn("Bad", 8, true);
    table.AddColumn("Not", 8, true);
    table.AddColumn("Total(ns)", 14, true);
    table.AddColumn("Avg(ns)", 12, true);
    table.AddColumn("Min(ns)", 12, true);
    table.AddColumn("Max(ns)", 12, true);
#ifdef SPDIAG_ENABLE_PERCENTILE
    table.AddColumn("P99(ns)", 12, true);
    table.AddColumn("P999(ns)", 12, true);
    table.AddColumn("P9999(ns)", 12, true);
#endif

    int rowIdx = 1;
    for (const auto& st : record.stats) {
        uint64_t avgNs = st.deltaTickCount > 0 ? st.deltaTimeNs / st.deltaTickCount : 0;
        std::vector<std::string> cells = {
            std::to_string(rowIdx),
            st.moduleName,
            st.pointName,
            std::to_string(static_cast<int>(st.level)),
            std::to_string(st.deltaTickCount),
            std::to_string(st.deltaGoodCount),
            std::to_string(st.deltaBadCount),
            std::to_string(st.deltaNotCount),
            FormatNs(st.deltaTimeNs, 14),
            FormatNs(avgNs, 12),
            FormatNs(st.minTimeNs, 12),
            FormatNs(st.maxTimeNs, 12)
        };
#ifdef SPDIAG_ENABLE_PERCENTILE
        cells.push_back(FormatPercentile(st.p99Ns, st.hasP99, 12));
        cells.push_back(FormatPercentile(st.p999Ns, st.hasP999, 12));
        cells.push_back(FormatPercentile(st.p9999Ns, st.hasP9999, 12));
#endif
        table.AddRow(cells);
        ++rowIdx;
    }
    table.Print();
    std::cout << "\n";
    csvWriter_.EndSession();
}

void DisplayEngine::PrintSummaryHeader() {
    Cli::TextTable table;
    table.AddColumn("#", 4, true);
    table.AddColumn("Program", 16);
    table.AddColumn("Module", 16);
    table.AddColumn("Point", 20);
    table.AddColumn("Lvl", 3, true);
    table.AddColumn("Ticks", 8, true);
    table.AddColumn("Good", 8, true);
    table.AddColumn("Bad", 8, true);
    table.AddColumn("Not", 8, true);
    table.AddColumn("Total(ns)", 14, true);
    table.AddColumn("Avg(ns)", 12, true);
    table.AddColumn("Min(ns)", 12, true);
    table.AddColumn("Max(ns)", 12, true);
#ifdef SPDIAG_ENABLE_PERCENTILE
    table.AddColumn("P99(ns)", 12, true);
    table.AddColumn("P999(ns)", 12, true);
    table.AddColumn("P9999(ns)", 12, true);
#endif
    table.PrintHeader();
}

void DisplayEngine::PrintDetailHeader() {
    Cli::TextTable table;
    table.AddColumn("#", 4, true);
    table.AddColumn("Core", 4, true);
    table.AddColumn("Program", 16);
    table.AddColumn("Name", 28);
    table.AddColumn("Lvl", 3, true);
    table.AddColumn("Ticks", 8, true);
    table.AddColumn("Good", 8, true);
    table.AddColumn("Bad", 8, true);
    table.AddColumn("Not", 8, true);
    table.AddColumn("Total(ns)", 14, true);
    table.AddColumn("Avg(ns)", 12, true);
    table.AddColumn("Min(ns)", 12, true);
    table.AddColumn("Max(ns)", 12, true);
#ifdef SPDIAG_ENABLE_PERCENTILE
    table.AddColumn("P99(ns)", 12, true);
    table.AddColumn("P999(ns)", 12, true);
    table.AddColumn("P9999(ns)", 12, true);
#endif
    table.PrintHeader();
}
double DisplayEngine::ComputeSession() {
    uint64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (memSessionStartNs_ == 0) {
        memSessionStartNs_ = nowNs;
    }
    return static_cast<double>(nowNs - memSessionStartNs_) / 1e9;
}

void DisplayEngine::ShowEmptySummary() {
    csvWriter_.BeginSession("spdiag_perfpoint_summary");
    PrintSummaryHeader();
    csvWriter_.EndSession();
}

void DisplayEngine::ShowEmptyDetail() {
    csvWriter_.BeginSession("spdiag_perfpoint_detail");
    PrintDetailHeader();
    csvWriter_.EndSession();
}


void DisplayEngine::ShowObMemory(const std::vector<ProcessorOutput>& outputs, bool humanReadable, bool noFilter, bool detail, const std::string& groupBy, int top, const std::string& showFields, const std::vector<Cli::SortKey>& sortKeys, const std::vector<std::string>& filterGlobs) {
    auto rawEntries = ParseMemEntries(outputs);
    double sessionSec = ComputeSession();

    csvWriter_.BeginSession("spdiag_memstat");

    std::string effectiveGroupBy = groupBy;
    if (effectiveGroupBy != "tid+frame" && effectiveGroupBy != "frame" && effectiveGroupBy != "raw") {
        effectiveGroupBy = "raw";
    }

    bool isAggregated = (effectiveGroupBy == "tid+frame" || effectiveGroupBy == "frame");
    if (isAggregated) {
        ShowMemAggregated(rawEntries, effectiveGroupBy, sessionSec, humanReadable, noFilter, detail, top, showFields, sortKeys, filterGlobs);
    } else {
        ComputeChurn(rawEntries, sessionSec);
        ShowMemRaw(std::move(rawEntries), noFilter, humanReadable, detail, top, showFields, sortKeys, filterGlobs);
    }

    std::cout << "\n";
    csvWriter_.EndSession();
}

void DisplayEngine::ShowObCache(const std::vector<ProcessorOutput>& outputs, bool cumulative) {
    csvWriter_.BeginSession("spdiag_cachestat");
    ShowObCacheDisplay(outputs, cumulative, cacheDisplayState_);
    csvWriter_.EndSession();
}

void DisplayEngine::ClearScreen() {
    std::cout << "\033[2J" << std::flush;
}

void DisplayEngine::MoveCursorHome() {
    std::cout << "\033[H" << std::flush;
}

void DisplayEngine::ShowMemPoint(const std::vector<ProcessorOutput>& outputs,
                                  bool humanReadable, int top,
                                  const std::vector<Cli::SortKey>& sortKeys,
                                  const std::vector<std::string>& filterGlobs,
                                  const std::string& showFields) {
    auto entries = ParseMemPointEntries(outputs);
    if (entries.empty()) return;

    double sessionSec = ComputeSession();
    ComputeChurn(entries, sessionSec);

    csvWriter_.BeginSession("spdiag_mempoint");
    ShowMemPointDisplay(entries, humanReadable, top, sortKeys, filterGlobs, showFields);
    ShowStructBreakdown(entries, humanReadable);
    csvWriter_.EndSession();
}

#ifdef SPDIAG_ENABLE_PERFLOG
void DisplayEngine::ShowPerfLog(const ShmInfo& info, const ShardBuffer& shards,
                                const ProgramEntry* programs, uint32_t numPrograms,
                                const char (*keyPointNames)[nameLen],
                                const char (*keyModuleNames)[nameLen]) {
    struct PerfLogRow {
        uint64_t timestampNs;
        uint64_t costNs;
        uint32_t pid;
        uint32_t threadId;
        uint32_t cpuId;
        uint8_t  level;
        const char* programName;
        const char* moduleName;
        const char* pointName;
    };

    std::vector<PerfLogRow> rows;
    rows.reserve(info.numCores * perflogTimestampCount);

    bool headerPrinted = false;

    csvWriter_.BeginSession("spdiag_perflog");

    Cli::TextTable table;
    table.AddColumn("Datetime", 34);
    table.AddColumn("PID", 6, true);
    table.AddColumn("TID", 8, true);
    table.AddColumn("CPU", 5, true);
    table.AddColumn("Level", 5, true);
    table.AddColumn("Program", 14);
    table.AddColumn("Module.Point", 28);
    table.AddColumn("Cost(ns)", 10, true);

    for (uint32_t p = 0; p < numPrograms; ++p) {
        const ProgramEntry& prog = programs[p];
        if (!prog.active) continue;

        uint32_t slotEnd = prog.baseSlot + prog.slotCount;
        if (slotEnd > info.maxSlots) slotEnd = info.maxSlots;

        for (uint32_t slot = prog.baseSlot; slot < slotEnd; ++slot) {
            if (slot >= info.maxSlots) continue;

            const char* ptName = (keyPointNames[slot][0] != '\0')
                                 ? keyPointNames[slot] : nullptr;
            if (!ptName) continue;

            const char* modName = (keyModuleNames[slot][0] != '\0')
                                  ? keyModuleNames[slot] : "-";

            rows.clear();
            for (uint32_t core = 0; core < info.numCores; ++core) {
                if (core >= shards.size()) break;
                const PerfStat& stat = shards.stats(core)[slot];
                uint32_t count = stat.perflogWritePos.load(std::memory_order_relaxed);
                if (count > perflogTimestampCount) count = perflogTimestampCount;
                for (uint32_t t = 0; t < count; ++t) {
                    if (stat.timestamps[t] != 0) {
                        PerfLogRow row;
                        row.timestampNs = stat.timestamps[t];
                        row.costNs = stat.perflogCosts[t];
                        row.pid = static_cast<uint32_t>(prog.pid);
                        row.threadId = stat.perflogThreadIds[t];
                        row.cpuId = core;
                        row.level = stat.level;
                        row.programName = prog.name;
                        row.moduleName = modName;
                        row.pointName = ptName;
                        rows.push_back(row);
                    }
                }
            }

            if (rows.empty()) continue;

            std::sort(rows.begin(), rows.end(),
                      [](const PerfLogRow& a, const PerfLogRow& b) {
                          return a.timestampNs < b.timestampNs;
                      });

            if (!headerPrinted) {
                headerPrinted = true;
                table.PrintHeader();
            }

            uint32_t total = static_cast<uint32_t>(rows.size());
            uint32_t keep = (total > perflogTimestampCount) ? perflogTimestampCount : total;
            uint32_t startIdx = total - keep;

            for (uint32_t i = 0; i < keep; ++i) {
                const PerfLogRow& r = rows[startIdx + i];

                uint64_t wallNs;
                if (info.perflogTimerFreq > 0) {
                    uint64_t deltaTicks = r.timestampNs - info.perflogBootCounter;
                    wallNs = deltaTicks * 1000000000ULL / info.perflogTimerFreq + info.perflogBootSystemNs;
                } else {
                    wallNs = (r.timestampNs - info.perflogBootCounter) + info.perflogBootSystemNs;
                }
                time_t sec = static_cast<time_t>(wallNs / 1000000000ULL);
                uint32_t ns = static_cast<uint32_t>(wallNs % 1000000000ULL);
                struct tm tmBuf;
                localtime_r(&sec, &tmBuf);
                char timeStr[64];
                std::snprintf(timeStr, sizeof(timeStr),
                              "%04d-%02d-%02d %02d:%02d:%02d.%09u",
                              tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, tmBuf.tm_mday,
                              tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec, ns);

                std::string fullName = std::string(r.moduleName) + "." + r.pointName;

                table.PrintRow({
                    std::string(timeStr),
                    std::to_string(r.pid),
                    std::to_string(r.threadId),
                    std::to_string(r.cpuId),
                    std::to_string(static_cast<int>(r.level)),
                    r.programName,
                    fullName,
                    std::to_string(r.costNs)
                });
            }
        }
    }
    csvWriter_.EndSession();
}
#endif

}
