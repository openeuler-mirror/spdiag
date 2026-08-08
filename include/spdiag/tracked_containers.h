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

#ifdef SPDIAG_ENABLE_MEMPOINT

#include "spdiag/tracked_allocator.h"

#include <deque>
#include <functional>
#include <list>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace SpDiag::tracked {

template <typename T, MemPointKey K>
using vector = std::vector<T, TrackedAllocator<T, K>>;

template <typename CharT, MemPointKey K>
using basic_string = std::basic_string<CharT, std::char_traits<CharT>,
                                       TrackedAllocator<CharT, K>>;

template <MemPointKey K>
using string = basic_string<char, K>;

template <typename Key, typename V, MemPointKey K,
          typename Hash = std::hash<Key>,
          typename Eq   = std::equal_to<Key>>
using unordered_map = std::unordered_map<Key, V, Hash, Eq,
    TrackedAllocator<std::pair<const Key, V>, K>>;

template <typename Key, MemPointKey K,
          typename Hash = std::hash<Key>,
          typename Eq   = std::equal_to<Key>>
using unordered_set = std::unordered_set<Key, Hash, Eq,
    TrackedAllocator<Key, K>>;

template <typename Key, typename V, MemPointKey K,
          typename Cmp = std::less<Key>>
using map = std::map<Key, V, Cmp,
    TrackedAllocator<std::pair<const Key, V>, K>>;

template <typename Key, MemPointKey K,
          typename Cmp = std::less<Key>>
using set = std::set<Key, Cmp, TrackedAllocator<Key, K>>;

template <typename T, MemPointKey K>
using deque = std::deque<T, TrackedAllocator<T, K>>;

template <typename T, MemPointKey K>
using list = std::list<T, TrackedAllocator<T, K>>;

} // namespace SpDiag::tracked

#endif // SPDIAG_ENABLE_MEMPOINT
