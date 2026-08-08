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
#include <string_view>

namespace SpDiag {
namespace Cli {

// Whole-string glob match. Supports * (any length), ? (single char), \ (escape).
// Pattern must match the entire str (not substring).
inline bool GlobMatch(std::string_view p, std::string_view s) {
    size_t pi = 0, si = 0;
    size_t star = std::string_view::npos, match = 0;
    while (si < s.size()) {
        if (pi < p.size() && p[pi] == '\\' && pi + 1 < p.size()) {
            if (p[pi + 1] == s[si]) { pi += 2; ++si; continue; }
        } else if (pi < p.size() && (p[pi] == '?' || p[pi] == s[si])) {
            ++pi; ++si; continue;
        } else if (pi < p.size() && p[pi] == '*') {
            star = pi++; match = si; continue;
        }
        if (star != std::string_view::npos) {
            pi = star + 1; si = ++match; continue;
        }
        return false;
    }
    while (pi < p.size() && p[pi] == '*') ++pi;
    return pi == p.size();
}

}  // namespace Cli
}  // namespace SpDiag
