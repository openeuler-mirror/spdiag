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

#include "memstat_display.h"
#include "format_util.h"
#include <iostream>
#include <cstdio>
#include <algorithm>
#include <unordered_map>
#include <set>
#include <cstdlib>

using namespace SpDiag::Cli;

namespace {

std::vector<SpDiag::FrameInfo> ParseStackFrames(const std::string& callStack) {
    std::vector<SpDiag::FrameInfo> frames;
    if (callStack.empty()) return frames;
    size_t start = 0;
    size_t end = 0;
    while ((end = callStack.find(" <- ", start)) != std::string::npos) {
        SpDiag::FrameInfo fi;
        fi.func = callStack.substr(start, end - start);
        frames.push_back(std::move(fi));
        start = end + 4;
    }
    SpDiag::FrameInfo fi;
    fi.func = callStack.substr(start);
    frames.push_back(std::move(fi));
    return frames;
}

}  // namespace

namespace SpDiag {
namespace Cli {

const std::vector<std::string>& ValidShowCols() {
    static const std::vector<std::string> cols = {
        "current", "total", "allocs", "frees", "peak",
        "min_size", "max_size", "first_seen", "leaked", "avg_size",
        "churn", "churn_rate", "alloc_rate"
    };
    return cols;
}

ShowColumns ParseShowColumns(const std::string& showFields) {
    ShowColumns result;
    if (showFields.empty()) return result;
    auto& valid = ValidShowCols();
    size_t p = 0, q = 0;
    while ((q = showFields.find(',', p)) != std::string::npos) {
        auto col = showFields.substr(p, q - p);
        bool found = false;
        for (const auto& v : valid) { if (v == col) { found = true; break; } }
        if (found) {
            result.order.push_back(col);
            result.set.insert(col);
        } else {
            std::cerr << "spdiag: --show: unknown column '" << col << "' (ignored)\n";
        }
        p = q + 1;
    }
    if (p < showFields.size()) {
        auto col = showFields.substr(p);
        bool found = false;
        for (const auto& v : valid) { if (v == col) { found = true; break; } }
        if (found) {
            result.order.push_back(col);
            result.set.insert(col);
        } else {
            std::cerr << "spdiag: --show: unknown column '" << col << "' (ignored)\n";
        }
    }
    return result;
}

std::string MemColHeader(const std::string& col, bool human) {
    if (col == "pid") return "PID";
    if (col == "tid") return "TID";
    if (col == "frame") return "CallStack";
    if (col == "current") return human ? "Current" : "Current(B)";
    if (col == "total") return human ? "Total" : "Total(B)";
    if (col == "allocs") return "Allocs";
    if (col == "frees") return "Frees";
    if (col == "peak") return human ? "Peak" : "Peak(B)";
    if (col == "min_size") return human ? "MinSize" : "MinSize(B)";
    if (col == "max_size") return human ? "MaxSize" : "MaxSize(B)";
    if (col == "first_seen") return "FirstSeen";
    if (col == "leaked") return "Leaked";
    if (col == "avg_size") return human ? "AvgSize" : "AvgSize(B)";
    if (col == "churn")      return human ? "Churn"      : "Churn(B)";
    if (col == "churn_rate") return "ChurnRate";
    if (col == "alloc_rate") return "AllocRate";
    return col;
}

int MemColWidth(const std::string& col, bool human) {
    if (col == "pid") return 5;
    if (col == "tid") return 7;
    if (col == "frame") return 45;
    if (col == "first_seen") return 9;
    if (col == "leaked") return 10;
    if (col == "churn")      return human ? 9 : 14;
    if (col == "churn_rate") return 10;
    if (col == "alloc_rate") return 10;
    if (col == "allocs" || col == "frees") return 10;
    if (human) return 9;
    return 14;
}

std::string PickStackSummaryStr(const std::string& callStack, bool /*noFilter*/) {
    auto frames = ParseStackFrames(callStack);
    frames = SpDiag::FrameFilter{}.Apply(frames);
    auto name = SpDiag::FrameFilter::PickHeaderFrame(frames);
    return name.empty() ? callStack : std::string(name);
}

void PrintStackTree(const std::string& callStack, bool noFilter) {
    if (callStack.empty()) return;
    auto frames = ParseStackFrames(callStack);
    auto filtered = noFilter ? frames : SpDiag::FrameFilter{}.Apply(frames);
    if (filtered.empty()) return;

    auto headerName = SpDiag::FrameFilter::PickHeaderFrame(filtered);

    size_t allocPoint = filtered.size();
    for (size_t i = 1; i < filtered.size(); ++i) {
        if (SpDiag::FrameFilter::IsAllocatorFunc(filtered[i].func)) {
            allocPoint = i - 1;
            break;
        }
    }

    std::cout << "         |\n";
    for (size_t i = 0; i < filtered.size(); ++i) {
        bool isHeader = (filtered[i].func == headerName);
        bool last = (i == filtered.size() - 1);
        std::string prefix = last ? "         └─ " : "         ├─ ";
        std::string marker = isHeader ? "\u25b6 " : "  ";
        std::string tag;
        if (i == allocPoint) tag = "  <-- allocation point";
        std::cout << prefix << marker << filtered[i].func << tag << "\n";
    }
}

void PrintNoFilterStack(const std::string& callStack) {
    if (callStack.empty() || callStack.find(" <- ") == std::string::npos) return;
    auto frames = ParseStackFrames(callStack);
    if (frames.empty()) return;
    std::cout << "         ";
    for (size_t i = 0; i < frames.size(); ++i) {
        if (i > 0) std::cout << " <- ";
        std::cout << frames[i].func;
    }
    std::cout << "\n";
}

void PrintDetailMembers(const std::vector<MemEntry>& rawRecords,
                        bool humanReadable, bool noFilter) {
    struct MergedMember {
        std::string callStack;
        size_t count;
        uint64_t bytes;
    };
    std::vector<MergedMember> merged;
    std::unordered_map<std::string, size_t> stackIdx;
    for (const auto& re : rawRecords) {
        auto it = stackIdx.find(re.callStack);
        if (it != stackIdx.end()) {
            merged[it->second].count++;
            merged[it->second].bytes += re.currentBytes;
        } else {
            stackIdx[re.callStack] = merged.size();
            merged.push_back({re.callStack, 1, re.currentBytes});
        }
    }
    for (size_t mi = 0; mi < merged.size(); ++mi) {
        const auto& mm = merged[mi];
        if (merged.size() > 1) {
            std::cout << "            [member " << (mi + 1) << "/"
                      << merged.size() << "]\n";
        }
        if (mm.count > 1) {
            std::cout << "            (" << mm.count
                      << " identical stacks, "
                      << FormatSize(mm.bytes, humanReadable) << ")\n";
        }
        if (mm.callStack.find(" <- ") != std::string::npos) {
            PrintStackTree(mm.callStack, noFilter);
        }
    }
}

std::vector<MemEntry> ParseMemEntries(const std::vector<ProcessorOutput>& outputs) {
    std::unordered_map<std::string, MemEntry> entryMap;

    for (const auto& out : outputs) {
        const std::string& k = out.key;
        if (k.find("mem.pid.") != 0) continue;

        size_t dotTid = k.find(".tid.");
        if (dotTid == std::string::npos) continue;
        size_t dotStack = k.find(".stack.", dotTid + 5);
        if (dotStack == std::string::npos) continue;
        size_t dotField = k.find('.', dotStack + 7);
        if (dotField == std::string::npos) continue;

        std::string prefix = k.substr(0, dotField);
        std::string field = k.substr(dotField + 1);

        auto& entry = entryMap[prefix];
        entry.hasData = true;

        size_t pidStart = 8;
        size_t pidEnd = k.find('.', pidStart);
        if (pidEnd != std::string::npos) {
            entry.pid = static_cast<int32_t>(std::strtoul(
                k.substr(pidStart, pidEnd - pidStart).c_str(), nullptr, 10));
        }
        size_t tidStart = dotTid + 5;
        entry.tid = static_cast<int32_t>(std::strtoul(
            k.substr(tidStart, dotStack - tidStart).c_str(), nullptr, 10));
        size_t sidStart = dotStack + 7;
        entry.stackId = static_cast<int32_t>(std::strtol(
            k.substr(sidStart, dotField - sidStart).c_str(), nullptr, 10));

        if (field == "current_bytes") {
            entry.currentBytes = std::strtoull(out.value.c_str(), nullptr, 10);
        } else if (field == "total_alloc_bytes") {
            entry.totalBytes = std::strtoull(out.value.c_str(), nullptr, 10);
        } else if (field == "alloc_count") {
            entry.allocs = std::strtoull(out.value.c_str(), nullptr, 10);
        } else if (field == "free_count") {
            entry.frees = std::strtoull(out.value.c_str(), nullptr, 10);
        } else if (field == "peak_bytes") {
            entry.peakBytes = std::strtoull(out.value.c_str(), nullptr, 10);
        } else if (field == "max_size") {
            entry.maxSize = std::strtoull(out.value.c_str(), nullptr, 10);
        } else if (field == "min_size") {
            entry.minSize = std::strtoull(out.value.c_str(), nullptr, 10);
        } else if (field == "first_seen_ns") {
            entry.firstSeenNs = std::strtoull(out.value.c_str(), nullptr, 10);
        } else if (field == "stack_trace") {
            entry.callStack = out.value;
            if (entry.callStack.empty()) {
                entry.callStack = "<" + std::to_string(entry.stackId) + ">";
            }
        }
    }

    std::vector<MemEntry> rawEntries;
    for (auto& [key, entry] : entryMap) {
        if (entry.hasData) rawEntries.push_back(std::move(entry));
    }

    std::sort(rawEntries.begin(), rawEntries.end(),
        [](const MemEntry& a, const MemEntry& b) {
            return a.currentBytes > b.currentBytes;
        });

    return rawEntries;
}

std::vector<AggEntry> AggregateMemEntries(const std::vector<MemEntry>& rawEntries, const std::string& groupBy, bool noFilter) {
    std::unordered_map<std::string, size_t> aggIdx;
    std::vector<AggEntry> aggEntries;

    for (const auto& e : rawEntries) {
        std::string frameName;
        auto frames = ParseStackFrames(e.callStack);
        auto filtered = SpDiag::FrameFilter{}.Apply(frames);
        frameName = std::string(SpDiag::FrameFilter::PickHeaderFrame(filtered));
        if (frameName.empty()) frameName = e.callStack;

        std::string groupKey;
        if (noFilter) {
            groupKey = e.callStack;
        } else {
            groupKey = frameName;
        }
        std::string keyStr = std::to_string(e.pid);
        if (groupBy == "tid+frame") {
            keyStr += "." + std::to_string(e.tid);
        }
        keyStr += "." + groupKey;

        auto it = aggIdx.find(keyStr);
        if (it != aggIdx.end()) {
            auto& ae = aggEntries[it->second];
            ae.currentBytes += e.currentBytes;
            ae.totalBytes += e.totalBytes;
            ae.allocs += e.allocs;
            ae.frees += e.frees;
            if (e.peakBytes > ae.peakBytes) ae.peakBytes = e.peakBytes;
            if (e.maxSize > ae.maxSize) ae.maxSize = e.maxSize;
            if (e.minSize < ae.minSize) ae.minSize = e.minSize;
            if (e.firstSeenNs < ae.firstSeenNs) ae.firstSeenNs = e.firstSeenNs;
            auto prevTid = ae.rawRecords.front().tid;
            if (e.tid != prevTid) ++ae.tidCount;
            ae.rawRecords.push_back(e);
        } else {
            AggEntry ae;
            ae.pid = e.pid;
            ae.tid = e.tid;
            ae.tidCount = 1;
            ae.callStack = std::string(frameName);
            ae.currentBytes = e.currentBytes;
            ae.totalBytes = e.totalBytes;
            ae.allocs = e.allocs;
            ae.frees = e.frees;
            ae.peakBytes = e.peakBytes;
            ae.maxSize = e.maxSize;
            ae.minSize = e.minSize;
            ae.firstSeenNs = e.firstSeenNs;
            ae.rawRecords.push_back(e);
            aggIdx[keyStr] = aggEntries.size();
            aggEntries.push_back(std::move(ae));
        }
    }

    std::sort(aggEntries.begin(), aggEntries.end(),
        [](const AggEntry& a, const AggEntry& b) {
            return a.currentBytes > b.currentBytes;
        });

    return aggEntries;
}

void ShowMemAggregated(const std::vector<MemEntry>& rawEntries,
                       const std::string& effectiveGroupBy,
                       double sessionSec,
                       bool humanReadable, bool noFilter, bool detail,
                       int top, const std::string& showFields,
                       const std::vector<SortKey>& sortKeys,
                       const std::vector<std::string>& filterGlobs) {
    auto showCols = ParseShowColumns(showFields);

    auto aggEntries = AggregateMemEntries(rawEntries, effectiveGroupBy, noFilter);

    ComputeChurn(aggEntries, sessionSec);

    ApplyGlobFilter(aggEntries, filterGlobs, [](const AggEntry& ae) {
        return ae.callStack;
    });

    // Multi-key sort
    const auto& effKeys = sortKeys.empty()
        ? DefaultMemstatSortKeys() : sortKeys;
    SortAccessorMap<AggEntry> aggAccessors = {
        { SortKey::CURRENT, [](const AggEntry& a, const AggEntry& b) {
            return ThreeWayCompare(a.currentBytes, b.currentBytes);
        }},
        { SortKey::PEAK, [](const AggEntry& a, const AggEntry& b) {
            return ThreeWayCompare(a.peakBytes, b.peakBytes);
        }},
        { SortKey::TOTAL, [](const AggEntry& a, const AggEntry& b) {
            return ThreeWayCompare(a.totalBytes, b.totalBytes);
        }},
        { SortKey::ALLOCS, [](const AggEntry& a, const AggEntry& b) {
            return ThreeWayCompare(a.allocs, b.allocs);
        }},
        { SortKey::FREES, [](const AggEntry& a, const AggEntry& b) {
            return ThreeWayCompare(a.frees, b.frees);
        }},
        { SortKey::MAX_SIZE, [](const AggEntry& a, const AggEntry& b) {
            return ThreeWayCompare(a.maxSize, b.maxSize);
        }},
        { SortKey::MIN_SIZE, [](const AggEntry& a, const AggEntry& b) {
            uint64_t av = (a.minSize == UINT64_MAX) ? 0 : a.minSize;
            uint64_t bv = (b.minSize == UINT64_MAX) ? 0 : b.minSize;
            return ThreeWayCompare(av, bv);
        }},
        { SortKey::FIRST_SEEN, [](const AggEntry& a, const AggEntry& b) {
            uint64_t av = (a.firstSeenNs == UINT64_MAX) ? 0 : a.firstSeenNs;
            uint64_t bv = (b.firstSeenNs == UINT64_MAX) ? 0 : b.firstSeenNs;
            return ThreeWayCompare(av, bv);
        }},
        { SortKey::LEAKED, [](const AggEntry& a, const AggEntry& b) {
            int64_t al = static_cast<int64_t>(a.allocs - a.frees);
            int64_t bl = static_cast<int64_t>(b.allocs - b.frees);
            return ThreeWayCompare(al, bl);
        }},
        { SortKey::AVG_SIZE, [](const AggEntry& a, const AggEntry& b) {
            double aa = a.allocs > 0 ? static_cast<double>(a.totalBytes) / a.allocs : 0.0;
            double bb = b.allocs > 0 ? static_cast<double>(b.totalBytes) / b.allocs : 0.0;
            return ThreeWayCompare(aa, bb);
        }},
        { SortKey::FRAME, [](const AggEntry& a, const AggEntry& b) {
            return a.callStack.compare(b.callStack);
        }},
    };
    ApplyMultiSort(aggEntries, effKeys, aggAccessors);

    size_t shown = static_cast<size_t>(top);
    if (shown > aggEntries.size()) shown = aggEntries.size();

    std::cout << "\n";
    if (!showCols.order.empty()) {
        // Custom columns from --show, with fixed PID/TID/CallStack prefix
        TextTable table;
        table.AddColumn("#", 4);
        table.AddColumn("PID", 5);
        table.AddColumn("TID", 7);
        table.AddColumn("CallStack", 45);
        for (const auto& col : showCols.order) {
            table.AddColumn(MemColHeader(col, humanReadable), MemColWidth(col, humanReadable));
        }
        table.PrintHeader();

        int rowIdx = 1;
        for (size_t i = 0; i < shown; ++i) {
            const auto& ae = aggEntries[i];
            std::string tidStr;
            if (effectiveGroupBy == "frame") {
                if (ae.tidCount >= 2)
                    tidStr = "(" + std::to_string(ae.tidCount) + " threads)";
                else
                    tidStr = std::to_string(ae.tid);
            } else {
                tidStr = std::to_string(ae.tid);
            }

            std::vector<std::string> cells;
            cells.push_back(std::to_string(rowIdx));
            cells.push_back(std::to_string(ae.pid));
            cells.push_back(tidStr);
            cells.push_back(Ellipsize(ae.callStack, 45));
            for (const auto& col : showCols.order) {
                cells.push_back(FormatAggCol(col, ae, tidStr, humanReadable));
            }
            table.PrintRow(cells);

            if (noFilter && !detail && !ae.rawRecords.empty()) PrintNoFilterStack(ae.rawRecords.front().callStack);

            if (detail) { PrintDetailMembers(ae.rawRecords, humanReadable, noFilter); }
            ++rowIdx;
        }
    } else {
        TextTable table;
        AddMemstatDefaultColumns(table, humanReadable);
        table.PrintHeader();

        int rowIdx = 1;
        for (size_t i = 0; i < shown; ++i) {
            const auto& ae = aggEntries[i];
            uint64_t avgSz = ae.allocs > 0 ? ae.totalBytes / ae.allocs : 0;

            std::string tidStr;
            if (effectiveGroupBy == "frame") {
                if (ae.tidCount >= 2)
                    tidStr = "(" + std::to_string(ae.tidCount) + " threads)";
                else
                    tidStr = std::to_string(ae.tid);
            } else {
                tidStr = std::to_string(ae.tid);
            }

            table.PrintRow({
                std::to_string(rowIdx),
                std::to_string(ae.pid),
                tidStr,
                Ellipsize(ae.callStack, 45),
                FormatSize(ae.currentBytes, humanReadable),
                FormatSize(ae.totalBytes, humanReadable),
                FormatComma(ae.allocs),
                FormatComma(ae.frees),
                FormatSize(ae.peakBytes, humanReadable),
                FormatSize(avgSz, humanReadable),
                FormatSize(ae.maxSize, humanReadable),
                FormatFirstSeen(ae.firstSeenNs)
            });

            if (noFilter && !detail && !ae.rawRecords.empty()) PrintNoFilterStack(ae.rawRecords.front().callStack);

            if (detail) { PrintDetailMembers(ae.rawRecords, humanReadable, noFilter); }
            ++rowIdx;
        }
    }

    PrintOverflowSummary(aggEntries, shown, "groups", "peak*", humanReadable);

    std::cout << "\n* Peak in aggregated views is max(per-record peak); may underestimate true peak.\n";

    {
        uint64_t tBytes = 0, tAllocs = 0, tFrees = 0;
        for (const auto& e : rawEntries) {
            tBytes += e.currentBytes;
            tAllocs += e.allocs;
            tFrees += e.frees;
        }
        if (humanReadable) {
            std::cerr << "[aggregator] groups=" << aggEntries.size()
                      << " (from " << rawEntries.size() << " raw records)"
                      << ", sum=" << FormatSize(tBytes, true)
                      << ", allocs=" << tAllocs
                      << ", frees=" << tFrees << std::endl;
        } else {
            std::cerr << "[aggregator] groups=" << aggEntries.size()
                      << " (from " << rawEntries.size() << " raw records)"
                      << ", sum bytes=" << tBytes
                      << " allocs=" << tAllocs
                      << " frees=" << tFrees << std::endl;
        }
    }
}

void ShowMemRaw(std::vector<MemEntry> entries,
                bool noFilter,
                bool humanReadable, bool detail,
                int top, const std::string& showFields,
                const std::vector<SortKey>& sortKeys,
                const std::vector<std::string>& filterGlobs) {
    auto showCols = ParseShowColumns(showFields);

    ApplyGlobFilter(entries, filterGlobs, [&](const MemEntry& e) {
        return PickStackSummaryStr(e.callStack, noFilter);
    });

    // Multi-key sort
    const auto& effKeys = sortKeys.empty()
        ? DefaultMemstatSortKeys() : sortKeys;
    SortAccessorMap<MemEntry> rawAccessors = {
        { SortKey::CURRENT, [](const MemEntry& a, const MemEntry& b) {
            return ThreeWayCompare(a.currentBytes, b.currentBytes);
        }},
        { SortKey::PEAK, [](const MemEntry& a, const MemEntry& b) {
            return ThreeWayCompare(a.peakBytes, b.peakBytes);
        }},
        { SortKey::TOTAL, [](const MemEntry& a, const MemEntry& b) {
            return ThreeWayCompare(a.totalBytes, b.totalBytes);
        }},
        { SortKey::ALLOCS, [](const MemEntry& a, const MemEntry& b) {
            return ThreeWayCompare(a.allocs, b.allocs);
        }},
        { SortKey::FREES, [](const MemEntry& a, const MemEntry& b) {
            return ThreeWayCompare(a.frees, b.frees);
        }},
        { SortKey::MAX_SIZE, [](const MemEntry& a, const MemEntry& b) {
            return ThreeWayCompare(a.maxSize, b.maxSize);
        }},
        { SortKey::MIN_SIZE, [](const MemEntry& a, const MemEntry& b) {
            return ThreeWayCompare(a.minSize, b.minSize);
        }},
        { SortKey::FIRST_SEEN, [](const MemEntry& a, const MemEntry& b) {
            return ThreeWayCompare(a.firstSeenNs, b.firstSeenNs);
        }},
        { SortKey::LEAKED, [](const MemEntry& a, const MemEntry& b) {
            int64_t al = static_cast<int64_t>(a.allocs - a.frees);
            int64_t bl = static_cast<int64_t>(b.allocs - b.frees);
            return ThreeWayCompare(al, bl);
        }},
        { SortKey::AVG_SIZE, [](const MemEntry& a, const MemEntry& b) {
            double aa = a.allocs > 0 ? static_cast<double>(a.totalBytes) / a.allocs : 0.0;
            double bb = b.allocs > 0 ? static_cast<double>(b.totalBytes) / b.allocs : 0.0;
            return ThreeWayCompare(aa, bb);
        }},
        { SortKey::FRAME, [](const MemEntry& a, const MemEntry& b) {
            return a.callStack.compare(b.callStack);
        }},
    };
    ApplyMultiSort(entries, effKeys, rawAccessors);

    std::cout << "\n";
    size_t shown = static_cast<size_t>(top);
    if (shown > entries.size()) shown = entries.size();

    if (!showCols.order.empty()) {
        // Custom columns from --show, with fixed PID/TID/CallStack prefix
        TextTable table;
        table.AddColumn("#", 4);
        table.AddColumn("PID", 5);
        table.AddColumn("TID", 7);
        table.AddColumn("CallStack", 45);
        for (const auto& col : showCols.order) {
            table.AddColumn(MemColHeader(col, humanReadable), MemColWidth(col, humanReadable));
        }
        table.PrintHeader();

        int rowIdx = 1;
        for (size_t i = 0; i < shown; ++i) {
            const auto& e = entries[i];
            std::string shortStack = PickStackSummaryStr(e.callStack, noFilter);
            std::vector<std::string> cells;
            cells.push_back(std::to_string(rowIdx));
            cells.push_back(std::to_string(e.pid));
            cells.push_back(std::to_string(e.tid));
            cells.push_back(Ellipsize(shortStack, 45));
            for (const auto& col : showCols.order) {
                cells.push_back(FormatRawCol(col, e, humanReadable));
            }
            table.PrintRow(cells);

            if (noFilter && !detail) PrintNoFilterStack(e.callStack);

            if (detail && e.callStack.find(" <- ") != std::string::npos) {
                PrintStackTree(e.callStack, noFilter);
            }
            ++rowIdx;
        }
    } else {
        TextTable table;
        AddMemstatDefaultColumns(table, humanReadable);
        table.PrintHeader();

        int rowIdx = 1;
        for (size_t i = 0; i < shown; ++i) {
            const auto& e = entries[i];
            uint64_t avgSz = e.allocs > 0 ? e.totalBytes / e.allocs : 0;
            std::string shortStack = PickStackSummaryStr(e.callStack, noFilter);

            table.PrintRow({
                std::to_string(rowIdx),
                std::to_string(e.pid),
                std::to_string(e.tid),
                Ellipsize(shortStack, 45),
                FormatSize(e.currentBytes, humanReadable),
                FormatSize(e.totalBytes, humanReadable),
                FormatComma(e.allocs),
                FormatComma(e.frees),
                FormatSize(e.peakBytes, humanReadable),
                FormatSize(avgSz, humanReadable),
                FormatSize(e.maxSize, humanReadable),
                FormatFirstSeen(e.firstSeenNs)
            });

            if (noFilter && !detail) PrintNoFilterStack(e.callStack);

            if (detail && e.callStack.find(" <- ") != std::string::npos) {
                PrintStackTree(e.callStack, noFilter);
            }
            ++rowIdx;
        }
    }

    PrintOverflowSummary(entries, shown, "records", "peak", humanReadable);

    {
        uint64_t totalBytes = 0;
        uint64_t totalAllocs = 0;
        uint64_t totalFrees = 0;
        for (const auto& e : entries) {
            totalBytes += e.currentBytes;
            totalAllocs += e.allocs;
            totalFrees += e.frees;
        }
        if (humanReadable) {
            std::cerr << "[aggregator] " << entries.size() << " records, "
                      << "sum=" << FormatSize(totalBytes, true)
                      << ", allocs=" << totalAllocs
                      << ", frees=" << totalFrees << std::endl;
        } else {
            std::cerr << "[aggregator] " << entries.size() << " records, "
                      << "sum bytes=" << totalBytes
                      << " allocs=" << totalAllocs
                      << " frees=" << totalFrees << std::endl;
        }
    }
}

}  // namespace Cli
}  // namespace SpDiag
