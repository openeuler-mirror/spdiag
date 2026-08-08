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
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <set>
#include "format_util.h"
#include "sort_spec.h"
#include "sort_engine.h"
#include "glob_match.h"
#include "filter_util.h"
#include "text_table.h"
#include "frame_filter.h"
#include "runtime/data_pipeline.h"

namespace SpDiag {
namespace Cli {

// --- Structs ---

struct MemEntry {
    int32_t pid = 0;
    int32_t tid = 0;
    int32_t stackId = 0;
    std::string callStack;
    uint64_t currentBytes = 0;
    uint64_t totalBytes = 0;
    uint64_t allocs = 0;
    uint64_t frees = 0;
    uint64_t peakBytes = 0;
    uint64_t maxSize = 0;
    uint64_t minSize = 0;
    uint64_t firstSeenNs = 0;
    uint64_t churnBytes = 0;
    double sessionDurationSec = 0.0;
    bool hasData = false;
};

struct AggEntry {
    int32_t pid = 0;
    int32_t tid = 0;
    int32_t tidCount = 1;
    std::string callStack;
    uint64_t currentBytes = 0;
    uint64_t totalBytes = 0;
    uint64_t allocs = 0;
    uint64_t frees = 0;
    uint64_t peakBytes = 0;
    uint64_t maxSize = 0;
    uint64_t minSize = UINT64_MAX;
    uint64_t firstSeenNs = UINT64_MAX;
    uint64_t churnBytes = 0;
    double sessionDurationSec = 0.0;
    std::vector<MemEntry> rawRecords;
};

struct ShowColumns {
    std::vector<std::string> order;
    std::set<std::string> set;
};

// --- Function declarations (implemented in memstat_display.cpp) ---

const std::vector<std::string>& ValidShowCols();
ShowColumns ParseShowColumns(const std::string& showFields);
std::string MemColHeader(const std::string& col, bool human);
int MemColWidth(const std::string& col, bool human);

std::vector<MemEntry> ParseMemEntries(const std::vector<ProcessorOutput>& outputs);
std::vector<AggEntry> AggregateMemEntries(const std::vector<MemEntry>& rawEntries,
                                           const std::string& groupBy, bool noFilter);
void PrintDetailMembers(const std::vector<MemEntry>& rawRecords,
                        bool humanReadable, bool noFilter);

std::string PickStackSummaryStr(const std::string& callStack, bool noFilter);
void PrintStackTree(const std::string& callStack, bool noFilter);
void PrintNoFilterStack(const std::string& callStack);

void ShowMemAggregated(const std::vector<MemEntry>& rawEntries,
                       const std::string& effectiveGroupBy,
                       double sessionSec,
                       bool humanReadable, bool noFilter, bool detail,
                       int top, const std::string& showFields,
                       const std::vector<SortKey>& sortKeys,
                       const std::vector<std::string>& filterGlobs);
void ShowMemRaw(std::vector<MemEntry> entries,
                bool noFilter,
                bool humanReadable, bool detail,
                int top, const std::string& showFields,
                const std::vector<SortKey>& sortKeys,
                const std::vector<std::string>& filterGlobs);

// --- Template implementations (inline) ---

inline void AddMemstatDefaultColumns(TextTable& table, bool human) {
    table.AddColumn("#", 4, true);
    table.AddColumn("PID", 5, true);
    table.AddColumn("TID", 7);
    table.AddColumn("CallStack", 45);
    table.AddColumn(MemColHeader("current", human), MemColWidth("current", human), true);
    table.AddColumn(MemColHeader("total", human), MemColWidth("total", human), true);
    table.AddColumn("Allocs", 10, true);
    table.AddColumn("Frees", 10, true);
    table.AddColumn(MemColHeader("peak", human), MemColWidth("peak", human), true);
    table.AddColumn(MemColHeader("avg_size", human), MemColWidth("avg_size", human), true);
    table.AddColumn(MemColHeader("max_size", human), MemColWidth("max_size", human), true);
    table.AddColumn(MemColHeader("first_seen", human), MemColWidth("first_seen", human), true);
}

template <typename T>
void ComputeChurn(std::vector<T>& entries, double sessionSec) {
    for (auto& e : entries) {
        e.churnBytes = e.totalBytes >= e.currentBytes
            ? (e.totalBytes - e.currentBytes) : 0;
        e.sessionDurationSec = sessionSec;
    }
}

template <typename T>
void PrintOverflowSummary(const std::vector<T>& entries, size_t shown,
                           const std::string& itemLabel, const std::string& peakLabel,
                           bool humanReadable) {
    if (entries.size() <= shown) return;
    uint64_t oBytes = 0, oTotal = 0, oAllocs = 0, oFrees = 0, oPeak = 0;
    for (size_t i = shown; i < entries.size(); ++i) {
        oBytes += entries[i].currentBytes;
        oTotal += entries[i].totalBytes;
        oAllocs += entries[i].allocs;
        oFrees += entries[i].frees;
        if (entries[i].peakBytes > oPeak) oPeak = entries[i].peakBytes;
    }
    std::cerr << "[others] (" << (entries.size() - shown) << " " << itemLabel << ")"
              << "  current=" << FormatSize(oBytes, humanReadable)
              << "  total=" << FormatSize(oTotal, humanReadable)
              << "  allocs=" << oAllocs
              << "  frees=" << oFrees
              << "  " << peakLabel << "=" << FormatSize(oPeak, humanReadable) << std::endl;
}

template <typename Entry>
std::string FormatMemCol(const std::string& col, const Entry& e, bool human) {
    if (col == "current") return FormatSize(e.currentBytes, human);
    if (col == "total") return FormatSize(e.totalBytes, human);
    if (col == "allocs") return FormatComma(e.allocs);
    if (col == "frees") return FormatComma(e.frees);
    if (col == "peak") return FormatSize(e.peakBytes, human);
    if (col == "min_size") {
        uint64_t v = (e.minSize == UINT64_MAX) ? 0 : e.minSize;
        return FormatSize(v, human);
    }
    if (col == "max_size") return FormatSize(e.maxSize, human);
    if (col == "first_seen") return FormatFirstSeen(e.firstSeenNs);
    if (col == "leaked") {
        int64_t v = static_cast<int64_t>(e.allocs) - static_cast<int64_t>(e.frees);
        return FormatComma(static_cast<uint64_t>(v > 0 ? v : 0));
    }
    if (col == "avg_size") {
        uint64_t v = e.allocs > 0 ? e.totalBytes / e.allocs : 0;
        return FormatSize(v, human);
    }
    if (col == "churn") return FormatSize(e.churnBytes, human);
    if (col == "churn_rate") return FormatRate(e.churnBytes, e.sessionDurationSec);
    if (col == "alloc_rate") return FormatRate(e.allocs, e.sessionDurationSec);
    return "?";
}

// --- Inline wrappers ---

inline std::string FormatAggCol(const std::string& col, const AggEntry& ae,
                                 const std::string& tidStr, bool human) {
    if (col == "pid") return std::to_string(ae.pid);
    if (col == "tid") return tidStr;
    if (col == "frame") return ae.callStack;
    return FormatMemCol(col, ae, human);
}

inline std::string FormatRawCol(const std::string& col, const MemEntry& e, bool human) {
    if (col == "pid") return std::to_string(e.pid);
    if (col == "tid") return std::to_string(e.tid);
    if (col == "frame") return e.callStack;
    return FormatMemCol(col, e, human);
}

}  // namespace Cli
}  // namespace SpDiag
