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

#ifdef SPDIAG_ENABLE_PERCENTILE

#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <functional>

namespace SpDiag {

inline void CalculatePercentiles(
    const std::vector<uint64_t>& topN,
    uint64_t totalTickCount,
    uint64_t& p99Ns, bool& hasP99,
    uint64_t& p999Ns, bool& hasP999,
    uint64_t& p9999Ns, bool& hasP9999)
{
    p99Ns = 0;
    hasP99 = false;
    p999Ns = 0;
    hasP999 = false;
    p9999Ns = 0;
    hasP9999 = false;

    if (totalTickCount == 0 || topN.empty()) {
        return;
    }

    size_t p99Index = static_cast<size_t>(std::floor(0.01 * totalTickCount));
    if (p99Index < topN.size()) {
        p99Ns = topN[p99Index];
        hasP99 = true;
    }

    size_t p999Index = static_cast<size_t>(std::floor(0.001 * totalTickCount));
    if (p999Index < topN.size()) {
        p999Ns = topN[p999Index];
        hasP999 = true;
    }

    size_t p9999Index = static_cast<size_t>(std::floor(0.0001 * totalTickCount));
    if (p9999Index < topN.size()) {
        p9999Ns = topN[p9999Index];
        hasP9999 = true;
    }
}

inline void CalculatePercentilesFromMinHeap(
    const uint64_t* heap, size_t heapSize, uint64_t totalTickCount,
    uint64_t& p99Ns, bool& hasP99,
    uint64_t& p999Ns, bool& hasP999,
    uint64_t& p9999Ns, bool& hasP9999)
{
    std::vector<uint64_t> sortedTopN;
    sortedTopN.reserve(heapSize);
    for (size_t i = 0; i < heapSize; ++i) {
        if (heap[i] > 0) {
            sortedTopN.push_back(heap[i]);
        }
    }
    std::sort(sortedTopN.begin(), sortedTopN.end(), std::greater<uint64_t>());
    CalculatePercentiles(sortedTopN, totalTickCount,
                         p99Ns, hasP99,
                         p999Ns, hasP999,
                         p9999Ns, hasP9999);
}

inline void CalculatePercentilesUnsorted(
    std::vector<uint64_t>& candidates, uint64_t totalTickCount,
    uint64_t& p99Ns, bool& hasP99,
    uint64_t& p999Ns, bool& hasP999,
    uint64_t& p9999Ns, bool& hasP9999,
    uint64_t truncationFloor = 0, bool coverageUnknown = false)
{
    p99Ns = p999Ns = p9999Ns = 0;
    hasP99 = hasP999 = hasP9999 = false;
    auto select = [&](double tail, uint64_t& value, bool& available) {
        const size_t index = static_cast<size_t>(std::floor(
            tail * totalTickCount));
        if (coverageUnknown || totalTickCount == 0 ||
            index >= candidates.size()) return;
        std::nth_element(candidates.begin(), candidates.begin() + index,
                         candidates.end(), std::greater<uint64_t>());
        if (candidates[index] < truncationFloor) return;
        value = candidates[index];
        available = true;
    };
    select(0.01, p99Ns, hasP99);
    select(0.001, p999Ns, hasP999);
    select(0.0001, p9999Ns, hasP9999);
}

}

#endif
