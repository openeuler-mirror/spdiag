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

#include "mempoint_display.h"
#include "spdiag/mem_point_keys.h"
#include "csv_writer.h"
#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <cstdlib>
#include <sstream>
#include <iomanip>

using namespace SpDiag::Cli;

namespace SpDiag {
namespace Cli {

std::vector<MemPointEntry> ParseMemPointEntries(
    const std::vector<ProcessorOutput>& outputs) {
    std::unordered_map<std::string, MemPointEntry> entryMap;

    for (const auto& out : outputs) {
        const std::string& k = out.key;
        if (k.find("mempoint.") != 0) continue;

        size_t pos = 9;
        size_t dotPid = k.find('.', pos);
        if (dotPid == std::string::npos) continue;

        std::string pidStr = k.substr(pos, dotPid - pos);
        int32_t pid = static_cast<int32_t>(std::strtol(pidStr.c_str(), nullptr, 10));

        size_t afterPid = dotPid + 1;
        bool isLeak = (k.compare(afterPid, 5, "leak.") == 0);

        if (isLeak) {
            size_t idStart = afterPid + 5;
            size_t dotField = k.find('.', idStart);
            if (dotField == std::string::npos) continue;

            std::string idStr = k.substr(idStart, dotField - idStart);
            uint32_t id = static_cast<uint32_t>(std::strtoul(idStr.c_str(), nullptr, 10));
            std::string field = k.substr(dotField + 1);

            std::string prefix = k.substr(0, dotField);
            auto& entry = entryMap[prefix];
            entry.pid = pid;
            entry.id = id;
            entry.hasData = true;

            if (field == "count") {
                entry.leakCount = std::strtoull(out.value.c_str(), nullptr, 10);
            } else if (field == "max_age_ns") {
                entry.leakMaxAgeNs = std::strtoull(out.value.c_str(), nullptr, 10);
            }
        } else {
            size_t dotField = k.find('.', afterPid);
            if (dotField == std::string::npos) continue;

            std::string idStr = k.substr(afterPid, dotField - afterPid);
            uint32_t id = static_cast<uint32_t>(std::strtoul(idStr.c_str(), nullptr, 10));
            std::string field = k.substr(dotField + 1);

            std::string prefix = k.substr(0, dotField);
            auto& entry = entryMap[prefix];
            entry.pid = pid;
            entry.id = id;
            entry.hasData = true;

            auto key = static_cast<MemPointKey>(id);
            if (id > 0 && id < static_cast<uint32_t>(MemPointKey::MEM_POINT_KEY_COUNT)) {
                entry.category = GetMemPointKeyCategory(key);
                entry.keyName = GetMemPointKeyDescription(key);
            }

            if (field == "current_bytes") {
                entry.currentBytes = std::strtoull(out.value.c_str(), nullptr, 10);
            } else if (field == "total_alloc") {
                entry.totalBytes = std::strtoull(out.value.c_str(), nullptr, 10);
            } else if (field == "alloc_count") {
                entry.allocs = std::strtoull(out.value.c_str(), nullptr, 10);
            } else if (field == "free_count") {
                entry.frees = std::strtoull(out.value.c_str(), nullptr, 10);
            } else if (field == "peak") {
                entry.peakBytes = std::strtoull(out.value.c_str(), nullptr, 10);
            } else if (field == "max_size") {
                entry.maxSize = std::strtoull(out.value.c_str(), nullptr, 10);
            } else if (field == "min_size") {
                entry.minSize = std::strtoull(out.value.c_str(), nullptr, 10);
            } else if (field == "first_seen_ns") {
                entry.firstSeenNs = std::strtoull(out.value.c_str(), nullptr, 10);
            } else if (field == "struct.type_name") {
                entry.structTypeName = out.value;
            } else if (field == "struct.total_static") {
                entry.totalStatic = std::strtoull(out.value.c_str(), nullptr, 10);
            } else if (field == "struct.total_heap") {
                entry.totalHeap = std::strtoull(out.value.c_str(), nullptr, 10);
            } else if (field == "struct.padding_bytes") {
                entry.paddingBytes = std::strtoull(out.value.c_str(), nullptr, 10);
            } else if (field.rfind("struct.field.", 0) == 0) {
                // Parse struct.field.{i}.{subfield}
                size_t fpos = 13; // after "struct.field."
                size_t dotSub = field.find('.', fpos);
                if (dotSub != std::string::npos) {
                    uint32_t fidx = static_cast<uint32_t>(
                        std::strtoul(field.substr(fpos, dotSub - fpos).c_str(), nullptr, 10));
                    std::string subfield = field.substr(dotSub + 1);
                    if (fidx < 16) {
                        if (entry.fields.size() <= fidx) {
                            entry.fields.resize(fidx + 1);
                        }
                        if (subfield == "name") {
                            entry.fields[fidx].name = out.value;
                        } else if (subfield == "static_size") {
                            entry.fields[fidx].staticSize = std::strtoull(out.value.c_str(), nullptr, 10);
                        } else if (subfield == "heap_size") {
                            entry.fields[fidx].heapSize = std::strtoull(out.value.c_str(), nullptr, 10);
                        } else if (subfield == "type") {
                            entry.fields[fidx].type = static_cast<uint8_t>(
                                std::strtoul(out.value.c_str(), nullptr, 10));
                        }
                    }
                }
            }
        }
    }

    std::unordered_map<std::string, MemPointEntry> merged;
    for (auto& [key, entry] : entryMap) {
        if (!entry.hasData) continue;
        std::string mergeKey = std::to_string(entry.pid) + "." + std::to_string(entry.id);
        merged[mergeKey].pid = entry.pid;
        merged[mergeKey].id = entry.id;
        merged[mergeKey].hasData = true;
    }

    for (auto& [key, entry] : entryMap) {
        if (!entry.hasData) continue;
        std::string mergeKey = std::to_string(entry.pid) + "." + std::to_string(entry.id);
        auto& target = merged[mergeKey];
        if (!entry.category.empty()) target.category = entry.category;
        if (!entry.keyName.empty()) target.keyName = entry.keyName;
        if (entry.currentBytes > 0 || entry.allocs > 0) {
            target.currentBytes = entry.currentBytes;
            target.totalBytes = entry.totalBytes;
            target.allocs = entry.allocs;
            target.frees = entry.frees;
            target.peakBytes = entry.peakBytes;
            target.maxSize = entry.maxSize;
            target.minSize = entry.minSize;
            target.firstSeenNs = entry.firstSeenNs;
        }
        if (entry.leakCount > 0) {
            target.leakCount = entry.leakCount;
            target.leakMaxAgeNs = entry.leakMaxAgeNs;
        }
        if (!entry.structTypeName.empty()) {
            target.structTypeName = entry.structTypeName;
            target.totalStatic = entry.totalStatic;
            target.totalHeap = entry.totalHeap;
            target.paddingBytes = entry.paddingBytes;
            if (!entry.fields.empty()) {
                target.fields = entry.fields;
            }
        }
    }

    std::vector<MemPointEntry> result;
    for (auto& [key, entry] : merged) {
        if (entry.hasData) result.push_back(std::move(entry));
    }
    return result;
}

void ShowMemPointDisplay(
    std::vector<MemPointEntry>& entries,
    bool humanReadable, int top,
    const std::vector<SortKey>& sortKeys,
    const std::vector<std::string>& filterGlobs,
    const std::string& showFields) {

    ApplyGlobFilter(entries, filterGlobs, [](const MemPointEntry& e) {
        return e.category + "." + e.keyName;
    });

    const auto& mpSortKeys = sortKeys.empty()
        ? DefaultMemPointSortKeys() : sortKeys;
    SortAccessorMap<MemPointEntry> memPointAccessors = {
        { SortKey::CURRENT, [](const MemPointEntry& a, const MemPointEntry& b) {
            return ThreeWayCompare(a.currentBytes, b.currentBytes);
        }},
        { SortKey::PEAK, [](const MemPointEntry& a, const MemPointEntry& b) {
            return ThreeWayCompare(a.peakBytes, b.peakBytes);
        }},
        { SortKey::TOTAL, [](const MemPointEntry& a, const MemPointEntry& b) {
            return ThreeWayCompare(a.totalBytes, b.totalBytes);
        }},
        { SortKey::ALLOCS, [](const MemPointEntry& a, const MemPointEntry& b) {
            return ThreeWayCompare(a.allocs, b.allocs);
        }},
        { SortKey::FREES, [](const MemPointEntry& a, const MemPointEntry& b) {
            return ThreeWayCompare(a.frees, b.frees);
        }},
        { SortKey::MAX_SIZE, [](const MemPointEntry& a, const MemPointEntry& b) {
            return ThreeWayCompare(a.maxSize, b.maxSize);
        }},
        { SortKey::MIN_SIZE, [](const MemPointEntry& a, const MemPointEntry& b) {
            return ThreeWayCompare(a.minSize, b.minSize);
        }},
        { SortKey::LEAKED, [](const MemPointEntry& a, const MemPointEntry& b) {
            uint64_t al = (a.allocs >= a.frees) ? (a.allocs - a.frees) : 0;
            uint64_t bl = (b.allocs >= b.frees) ? (b.allocs - b.frees) : 0;
            return ThreeWayCompare(al, bl);
        }},
        { SortKey::AVG_SIZE, [](const MemPointEntry& a, const MemPointEntry& b) {
            double aa = a.allocs > 0 ? static_cast<double>(a.totalBytes) / a.allocs : 0.0;
            double bb = b.allocs > 0 ? static_cast<double>(b.totalBytes) / b.allocs : 0.0;
            return ThreeWayCompare(aa, bb);
        }},
        { SortKey::FIRST_SEEN, [](const MemPointEntry& a, const MemPointEntry& b) {
            return ThreeWayCompare(a.firstSeenNs, b.firstSeenNs);
        }},
    };
    ApplyMultiSort(entries, mpSortKeys, memPointAccessors);

    if (top > 0 && static_cast<int>(entries.size()) > top) {
        entries.resize(static_cast<size_t>(top));
    }

    int pid = entries.empty() ? 0 : entries[0].pid;
    std::cout << "\nMemPoint Status (pid: " << pid << ")\n";

    auto showCols = ParseShowColumns(showFields);

    if (!showCols.order.empty()) {
        TextTable table;
        table.AddColumn("#", 4);
        table.AddColumn("Category", 10);
        table.AddColumn("Key", 22);
        for (const auto& col : showCols.order) {
            table.AddColumn(MemColHeader(col, humanReadable), MemColWidth(col, humanReadable));
        }
        table.PrintHeader();

        int rowIdx = 1;
        for (const auto& e : entries) {
            std::vector<std::string> cells;
            cells.push_back(std::to_string(rowIdx));
            cells.push_back(Ellipsize(e.category, 10));
            cells.push_back(Ellipsize(e.keyName, 22));
            for (const auto& col : showCols.order) {
                cells.push_back(FormatMemCol(col, e, humanReadable));
            }
            table.PrintRow(cells);
            ++rowIdx;
        }
    } else {
        TextTable table;
        table.AddColumn("#", 4, true);
        table.AddColumn("Category", 10);
        table.AddColumn("Key", 22);
        table.AddColumn(MemColHeader("current", humanReadable), MemColWidth("current", humanReadable), true);
        table.AddColumn(MemColHeader("total", humanReadable), MemColWidth("total", humanReadable), true);
        table.AddColumn("Allocs", 10, true);
        table.AddColumn("Frees", 10, true);
        table.AddColumn(MemColHeader("peak", humanReadable), MemColWidth("peak", humanReadable), true);
        table.AddColumn(MemColHeader("avg_size", humanReadable), MemColWidth("avg_size", humanReadable), true);
        table.AddColumn(MemColHeader("max_size", humanReadable), MemColWidth("max_size", humanReadable), true);
        table.AddColumn(MemColHeader("first_seen", humanReadable), MemColWidth("first_seen", humanReadable), true);
        table.PrintHeader();

        int rowIdx = 1;
        for (const auto& e : entries) {
            uint64_t avgSz = e.allocs > 0 ? e.totalBytes / e.allocs : 0;
            table.PrintRow({
                std::to_string(rowIdx),
                Ellipsize(e.category, 10),
                Ellipsize(e.keyName, 22),
                FormatSize(e.currentBytes, humanReadable),
                FormatSize(e.totalBytes, humanReadable),
                FormatComma(e.allocs),
                FormatComma(e.frees),
                FormatSize(e.peakBytes, humanReadable),
                FormatSize(avgSz, humanReadable),
                FormatSize(e.maxSize, humanReadable),
                FormatFirstSeen(e.firstSeenNs)
            });
            ++rowIdx;
        }
    }

    bool hasLeaks = false;
    for (const auto& e : entries) {
        if (e.leakCount > 0) { hasLeaks = true; break; }
    }
    if (hasLeaks) {
        std::cout << "\nLeak Detection:\n";
        TextTable table;
        table.AddColumn("Category", 10);
        table.AddColumn("Key", 22);
        table.AddColumn("Objects", 10, true);
        table.AddColumn("Max Age (min)", 14, true);
        table.PrintHeader();
        for (const auto& e : entries) {
            if (e.leakCount > 0) {
                double ageMin = static_cast<double>(e.leakMaxAgeNs) / 1e9 / 60.0;
                std::ostringstream ageStr;
                ageStr << std::fixed << std::setprecision(1) << ageMin;
                table.PrintRow({
                    Ellipsize(e.category, 10),
                    Ellipsize(e.keyName, 22),
                    FormatComma(e.leakCount),
                    ageStr.str()
                });
            }
        }
    }

    std::cout << std::endl;
}

void ShowStructBreakdown(
    std::vector<MemPointEntry>& entries,
    bool humanReadable) {
    bool hasStructs = false;
    for (const auto& e : entries) {
        if (!e.fields.empty()) { hasStructs = true; break; }
    }
    if (!hasStructs) return;

    std::cout << "\nStruct Memory Breakdown:\n";

    TextTable table;
    table.AddColumn("Type", 18);
    table.AddColumn("Field", 16);
    table.AddColumn("Static Size", 12, true);
    table.AddColumn("Heap Size", 12, true);
    table.AddColumn("Note", 6);
    table.PrintHeader();

    for (const auto& e : entries) {
        if (e.fields.empty()) continue;
        std::string typeName = e.structTypeName.empty() ? "unknown" : e.structTypeName;

        for (const auto& f : e.fields) {
            if (f.name.empty()) continue;
            std::string note;
            if (f.heapSize > 0) {
                note = "heap";
            } else if (f.type == 2) {
                note = "SSO";
            }
            table.PrintRow({
                typeName,
                f.name,
                FormatSize(f.staticSize, humanReadable),
                FormatSize(f.heapSize, humanReadable),
                note
            });
        }

        if (e.paddingBytes > 0) {
            table.PrintRow({
                typeName,
                "Padding",
                FormatSize(e.paddingBytes, humanReadable),
                "",
                ""
            });
        }

        uint64_t total = e.totalStatic + e.totalHeap;
        double staticPct = total > 0 ? (100.0 * e.totalStatic / total) : 0;
        double heapPct = total > 0 ? (100.0 * e.totalHeap / total) : 0;
        std::ostringstream pctStr;
        pctStr << "S:" << std::fixed << std::setprecision(1) << staticPct << "%"
               << " H:" << std::fixed << std::setprecision(1) << heapPct << "%";
        table.PrintRow({
            typeName,
            "TOTAL",
            FormatSize(e.totalStatic, humanReadable),
            FormatSize(e.totalHeap, humanReadable),
            pctStr.str()
        });
    }
    std::cout << "\n";
}

}  // namespace Cli
}  // namespace SpDiag
