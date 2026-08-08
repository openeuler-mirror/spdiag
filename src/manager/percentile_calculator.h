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

}

#endif
