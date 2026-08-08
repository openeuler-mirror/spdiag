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
#include <vector>
#include <string>
#include <algorithm>
#include "glob_match.h"

namespace SpDiag {
namespace Cli {

template <typename T, typename NameExtractor>
void ApplyGlobFilter(std::vector<T>& items,
                     const std::vector<std::string>& globs,
                     NameExtractor getName) {
    if (globs.empty()) return;
    items.erase(std::remove_if(items.begin(), items.end(),
        [&globs, &getName](const T& item) {
            std::string name = getName(item);
            for (const auto& pat : globs) {
                if (GlobMatch(pat, name)) return false;
            }
            return true;
        }), items.end());
}

}  // namespace Cli
}  // namespace SpDiag
