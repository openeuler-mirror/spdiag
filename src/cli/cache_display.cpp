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

#include "cache_display.h"
#include <iostream>
#include <cstdio>
#include <algorithm>
#include <cstdlib>

using namespace SpDiag::Cli;

namespace SpDiag {
namespace Cli {

// Render cachestat output: cache table + CPU performance table.
//   outputs — key/value pairs from the sampler (one Collect() cycle)
//   cumulative — true = accumulate deltas into running totals, display those
//   state — persists across calls (cumulative totals, session averages)
//
// Display has two sections:
//   1. Cache table: per-level references, misses, hit rate, MPKI, load density
//   2. CPU table: Delta (instantaneous) and Avg (session average) rows for IPC,
//      effective frequency, and GIPS.
//
// In cumulative mode, the cache table uses cumRef/cumMiss from state, and
// recomputes MPKI/loadDensity from cumInstructions (also from state).
// This ensures cumulative MPKI stays accurate even though the sampler only
// emits per-delta MPKI values.
void ShowObCacheDisplay(
    const std::vector<ProcessorOutput>& outputs,
    bool cumulative,
    CacheDisplayState& state) {

    struct CacheEntry {
        std::string level;
        uint64_t references = 0;
        uint64_t misses = 0;
        double hitRate = 0.0;
        double mpki = 0.0;
        double loadDensity = 0.0;
        bool hasRef = false;
        bool hasMiss = false;
        bool hasRate = false;
        bool hasMpki = false;
        bool hasLoadDensity = false;
    };

    struct CpuDelta {
        double ipc = 0.0;
        double freqGhz = 0.0;
        double gips = 0.0;
        bool hasData = false;
    };

    std::unordered_map<std::string, CacheEntry> entryMap;
    CpuDelta cpuDelta;
    bool multiplexed = false;
    std::string counterInfo;

    for (const auto& out : outputs) {
        const std::string& k = out.key;

        // CPU metrics
        if (k.find("cpu.") == 0) {
            if (k == "cpu.ipc") { cpuDelta.ipc = std::strtod(out.value.c_str(), nullptr); cpuDelta.hasData = true; }
            else if (k == "cpu.freq_ghz") { cpuDelta.freqGhz = std::strtod(out.value.c_str(), nullptr); }
            else if (k == "cpu.gips") { cpuDelta.gips = std::strtod(out.value.c_str(), nullptr); }
            else if (k == "cpu.avg_ipc") { state.avgIpc = std::strtod(out.value.c_str(), nullptr); state.hasAvg = true; }
            else if (k == "cpu.avg_freq_ghz") { state.avgFreqGhz = std::strtod(out.value.c_str(), nullptr); }
            else if (k == "cpu.avg_gips") { state.avgGips = std::strtod(out.value.c_str(), nullptr); }
            else if (k == "cpu.cum_instructions") { state.cumInstructions = std::strtoull(out.value.c_str(), nullptr, 10); }
            continue;
        }

        if (k.find("cache.") != 0) continue;

        if (k == "cache._counter_info") {
            counterInfo = out.value;
            continue;
        }
        if (k == "cache._multiplexed") {
            multiplexed = true;
            continue;
        }

        size_t dotPos = k.find('.', 6);
        if (dotPos == std::string::npos) continue;

        std::string level = k.substr(6, dotPos - 6);
        std::string field = k.substr(dotPos + 1);

        auto& entry = entryMap[level];

        if (field == "references") {
            entry.references = std::strtoull(out.value.c_str(), nullptr, 10);
            entry.hasRef = true;
        } else if (field == "misses") {
            entry.misses = std::strtoull(out.value.c_str(), nullptr, 10);
            entry.hasMiss = true;
        } else if (field == "hit_rate") {
            entry.hitRate = std::strtod(out.value.c_str(), nullptr);
            entry.hasRate = true;
        } else if (field == "mpki") {
            entry.mpki = std::strtod(out.value.c_str(), nullptr);
            entry.hasMpki = true;
        } else if (field == "load_density") {
            entry.loadDensity = std::strtod(out.value.c_str(), nullptr);
            entry.hasLoadDensity = true;
        }
        entry.level = level;
    }

    // Accumulate references/misses into running totals for cumulative mode.
    // cumInstructions comes from the sampler's cpu.cum_instructions key,
    // which accumulates across all deltas and persists after process exit.
    if (cumulative) {
        for (auto& [level, entry] : entryMap) {
            if (entry.hasRef) state.cumRef[level] += entry.references;
            if (entry.hasMiss) state.cumMiss[level] += entry.misses;
        }
    }

    // In cumulative mode, rebuild entries from accumulated totals.
    // Hit rate, MPKI, and load density are recomputed from the cumulative
    // values rather than using per-delta values, so they reflect the
    // true session aggregate. MPKI and load density require cumInstructions
    // which the sampler persists even after the target exits.
    std::vector<CacheEntry> entries;
    if (cumulative) {
        for (auto& [level, ref] : state.cumRef) {
            CacheEntry e;
            e.level = level;
            e.references = ref;
            e.misses = state.cumMiss[level];

            if (e.references > 0) {
                e.hitRate = 1.0 - static_cast<double>(e.misses) / static_cast<double>(e.references);
                if (e.hitRate < 0.0) e.hitRate = 0.0;
                e.hasRate = true;
            }
            if (state.cumInstructions > 0) {
                e.mpki = static_cast<double>(e.misses) * 1000.0 / static_cast<double>(state.cumInstructions);
                e.loadDensity = static_cast<double>(e.references) / static_cast<double>(state.cumInstructions);
                e.hasMpki = true;
                e.hasLoadDensity = true;
            }
            e.hasRef = true;
            e.hasMiss = true;
            entries.push_back(std::move(e));
        }
    } else {
        for (auto& [level, entry] : entryMap) {
            if (entry.hasRef || entry.hasMiss) {
                entries.push_back(std::move(entry));
            }
        }
    }

    // Deterministic row ordering: L1-dcache → L1-icache → LLC → dTLB → iTLB → branch
    static const std::vector<std::string> levelOrder = {"L1-dcache","L1-icache","LLC","dTLB","iTLB","branch"};
    std::sort(entries.begin(), entries.end(),
        [](const CacheEntry& a, const CacheEntry& b) {
            auto idx = [](const std::string& s) {
                auto it = std::find(levelOrder.begin(), levelOrder.end(), s);
                return it != levelOrder.end() ? std::distance(levelOrder.begin(), it) : levelOrder.size();
            };
            return idx(a.level) < idx(b.level);
        });

    if (cumulative) {
        std::cout << "\033[2J" << std::flush;
        std::cout << "\033[H" << std::flush;
    }

    // Cache table
    TextTable table;
    table.AddColumn("#", 4, true);
    table.AddColumn("Level", 10);
    table.AddColumn("References", 18, true);
    table.AddColumn("Misses", 18, true);
    table.AddColumn("MissRate(%)", 12, true);
    table.AddColumn("HitRate(%)", 12, true);
    table.AddColumn("MPKI", 10, true);
    table.AddColumn("LoadDens", 10, true);
    table.PrintHeader();

    int rowIdx = 1;
    for (const auto& e : entries) {
        std::string missRateStr, hitRateStr;
        if (e.references == 0) {
            missRateStr = "N/A";
            hitRateStr = "N/A";
        } else {
            double hitRatePct = e.hasRate ? (e.hitRate * 100.0) : (100.0 * static_cast<double>(e.references - e.misses) / static_cast<double>(e.references));
            double missRatePct = 100.0 - hitRatePct;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%10.2f", missRatePct);
            missRateStr = buf;
            std::snprintf(buf, sizeof(buf), "%10.2f", hitRatePct);
            hitRateStr = buf;
        }

        char mpkiBuf[16] = {};
        std::snprintf(mpkiBuf, sizeof(mpkiBuf), "%8.1f", e.mpki);

        char ldBuf[16] = {};
        std::snprintf(ldBuf, sizeof(ldBuf), "%8.3f", e.loadDensity);

        table.PrintRow({
            std::to_string(rowIdx),
            e.level,
            FormatComma(e.references),
            FormatComma(e.misses),
            missRateStr,
            hitRateStr,
            mpkiBuf,
            ldBuf,
        });
        ++rowIdx;
    }

    // Counter info footer
    if (!counterInfo.empty()) {
        std::cout << "Counters: " << counterInfo << " (events/PMU)";
        if (multiplexed) {
            std::cout << " -- MULTIPLEXED";
        }
        std::cout << "\n";
    }

    // CPU performance table: two rows for dual-mode display.
    //   Delta — instantaneous per-interval values from the sampler's cpu.ipc/freq/gips
    //   Avg   — session-average from cpu.avg_ipc/freq/gips, persists after exit
    // When the target exits, Delta stops emitting (delta=0), but Avg continues
    // to show the final session average from cumCycles/cumInstructions.
    if (cpuDelta.hasData || state.hasAvg) {
        std::cout << "\n";
        TextTable cpuTable;
        cpuTable.AddColumn("", 10);
        cpuTable.AddColumn("IPC", 8, true);
        cpuTable.AddColumn("Eff.Freq(GHz)", 14, true);
        cpuTable.AddColumn("GIPS", 10, true);
        cpuTable.PrintHeader();

        if (cpuDelta.hasData) {
            char ipcBuf[16] = {};
            std::snprintf(ipcBuf, sizeof(ipcBuf), "%6.2f", cpuDelta.ipc);
            char freqBuf[16] = {};
            std::snprintf(freqBuf, sizeof(freqBuf), "%12.2f", cpuDelta.freqGhz);
            char gipsBuf[16] = {};
            std::snprintf(gipsBuf, sizeof(gipsBuf), "%8.2f", cpuDelta.gips);
            cpuTable.PrintRow({"Delta", ipcBuf, freqBuf, gipsBuf});
        }

        if (state.hasAvg) {
            char ipcBuf[16] = {};
            std::snprintf(ipcBuf, sizeof(ipcBuf), "%6.2f", state.avgIpc);
            char freqBuf[16] = {};
            std::snprintf(freqBuf, sizeof(freqBuf), "%12.2f", state.avgFreqGhz);
            char gipsBuf[16] = {};
            std::snprintf(gipsBuf, sizeof(gipsBuf), "%8.2f", state.avgGips);
            cpuTable.PrintRow({"Avg", ipcBuf, freqBuf, gipsBuf});
        }
    }

    std::cout << std::flush;
}

}  // namespace Cli
}  // namespace SpDiag
