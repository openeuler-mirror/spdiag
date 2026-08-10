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
#include <functional>
#include <unordered_map>
#include <algorithm>
#include "sort_spec.h"

namespace SpDiag {
namespace Cli {

template <typename T>
using SortAccessorMap = std::unordered_map<SortKey::Key, std::function<int(const T&, const T&)>>;

template <typename T>
int ThreeWayCompare(T a, T b) { return (a < b) ? -1 : (a > b) ? 1 : 0; }

template <typename T>
void ApplyMultiSort(std::vector<T>& items,
                    const std::vector<SortKey>& keys,
                    const SortAccessorMap<T>& accessors) {
    if (keys.empty()) return;
    std::stable_sort(items.begin(), items.end(),
        [&keys, &accessors](const T& a, const T& b) {
            for (const auto& sk : keys) {
                auto it = accessors.find(sk.key);
                if (it == accessors.end()) continue;
                int cmp = it->second(a, b);
                if (cmp != 0) return sk.desc ? (cmp > 0) : (cmp < 0);
            }
            return false;
        });
}

}  // namespace Cli
}  // namespace SpDiag
