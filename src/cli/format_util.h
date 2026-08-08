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
#include <cstdint>
#include <cstdio>
#include <time.h>

namespace SpDiag { namespace Cli {

inline std::string FormatComma(uint64_t value) {
    std::string s = std::to_string(value);
    std::string result;
    int count = 0;
    for (int i = static_cast<int>(s.size()) - 1; i >= 0; --i) {
        if (count > 0 && count % 3 == 0) result.insert(result.begin(), ',');
        result.insert(result.begin(), s[i]);
        ++count;
    }
    return result;
}

inline std::string FormatHuman(uint64_t bytes) {
    const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    if (bytes < 1024) return std::to_string(bytes) + " B";

    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u + 1 < static_cast<int>(sizeof(units) / sizeof(units[0]))) {
        v /= 1024.0;
        ++u;
    }
    char buf[32];
    if (v < 10.0) {
        std::snprintf(buf, sizeof(buf), "%.2f %s", v, units[u]);
    } else if (v < 100.0) {
        std::snprintf(buf, sizeof(buf), "%.1f %s", v, units[u]);
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f %s", v, units[u]);
    }
    return std::string(buf);
}

inline std::string FormatSize(uint64_t value, bool human) {
    return human ? FormatHuman(value) : FormatComma(value) + " B";
}

inline std::string Ellipsize(const std::string& s, size_t maxLen) {
    if (s.size() <= maxLen) return s;
    return s.substr(0, maxLen - 1) + "\u2026";
}

inline std::string FormatRate(uint64_t value, double dtSec) {
    if (dtSec <= 0.0) return "?";
    double rate = static_cast<double>(value) / dtSec;
    if (rate >= 1e9)  return std::to_string(static_cast<uint64_t>(rate / 1e9)) + "G/s";
    if (rate >= 1e6)  return std::to_string(static_cast<uint64_t>(rate / 1e6)) + "M/s";
    if (rate >= 1e3)  return std::to_string(static_cast<uint64_t>(rate / 1e3)) + "K/s";
    return std::to_string(static_cast<uint64_t>(rate)) + "/s";
}

inline std::string FormatFirstSeen(uint64_t firstSeenNs) {
    if (firstSeenNs == 0 || firstSeenNs == UINT64_MAX) return "-";
    struct timespec monoTs = {}, realTs = {};
    clock_gettime(CLOCK_MONOTONIC, &monoTs);
    clock_gettime(CLOCK_REALTIME, &realTs);
    uint64_t monoNowNs = static_cast<uint64_t>(monoTs.tv_sec) * 1000000000ULL
                       + static_cast<uint64_t>(monoTs.tv_nsec);
    uint64_t realtimeNs = static_cast<uint64_t>(realTs.tv_sec) * 1000000000ULL
                        + static_cast<uint64_t>(realTs.tv_nsec);
    uint64_t offsetNs = (realtimeNs > monoNowNs) ? (realtimeNs - monoNowNs) : 0;
    uint64_t wallNs = firstSeenNs + offsetNs;
    time_t wallSec = static_cast<time_t>(wallNs / 1000000000ULL);
    struct tm tmBuf = {};
    localtime_r(&wallSec, &tmBuf);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
        tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec);
    return buf;
}

}} // namespace SpDiag::Cli
