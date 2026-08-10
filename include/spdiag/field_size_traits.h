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

#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <deque>
#include <list>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace SpDiag {

// Primary template: non-container types have no heap allocation.
template <typename T>
struct HeapSizeTraits {
    static uint64_t Get(const T&) noexcept { return 0; }
};

template <typename T, typename Alloc>
struct HeapSizeTraits<std::vector<T, Alloc>> {
    static uint64_t Get(const std::vector<T, Alloc>& v) noexcept {
        return static_cast<uint64_t>(v.capacity()) * sizeof(T);
    }
};

template <typename CharT, typename Traits, typename Alloc>
struct HeapSizeTraits<std::basic_string<CharT, Traits, Alloc>> {
    static uint64_t Get(const std::basic_string<CharT, Traits, Alloc>& s) noexcept {
        auto cap = s.capacity();
        return (cap > 15) ? static_cast<uint64_t>(cap + 1) * sizeof(CharT) : 0;
    }
};

template <typename K, typename V, typename Hash, typename Eq, typename Alloc>
struct HeapSizeTraits<std::unordered_map<K, V, Hash, Eq, Alloc>> {
    static uint64_t Get(const std::unordered_map<K, V, Hash, Eq, Alloc>& m) noexcept {
        return static_cast<uint64_t>(m.bucket_count()) * sizeof(void*)
             + static_cast<uint64_t>(m.size()) * sizeof(std::pair<const K, V>);
    }
};

template <typename K, typename Hash, typename Eq, typename Alloc>
struct HeapSizeTraits<std::unordered_set<K, Hash, Eq, Alloc>> {
    static uint64_t Get(const std::unordered_set<K, Hash, Eq, Alloc>& s) noexcept {
        return static_cast<uint64_t>(s.bucket_count()) * sizeof(void*)
             + static_cast<uint64_t>(s.size()) * sizeof(K);
    }
};

template <typename K, typename V, typename Cmp, typename Alloc>
struct HeapSizeTraits<std::map<K, V, Cmp, Alloc>> {
    static uint64_t Get(const std::map<K, V, Cmp, Alloc>& m) noexcept {
        return static_cast<uint64_t>(m.size()) * sizeof(std::pair<const K, V>)
             + static_cast<uint64_t>(m.size()) * (sizeof(void*) * 3);
    }
};

template <typename K, typename Cmp, typename Alloc>
struct HeapSizeTraits<std::set<K, Cmp, Alloc>> {
    static uint64_t Get(const std::set<K, Cmp, Alloc>& s) noexcept {
        return static_cast<uint64_t>(s.size()) * sizeof(K)
             + static_cast<uint64_t>(s.size()) * (sizeof(void*) * 3);
    }
};

template <typename T, typename Alloc>
struct HeapSizeTraits<std::deque<T, Alloc>> {
    static uint64_t Get(const std::deque<T, Alloc>& d) noexcept {
        return static_cast<uint64_t>(d.size()) * sizeof(T);
    }
};

template <typename T, typename Alloc>
struct HeapSizeTraits<std::list<T, Alloc>> {
    static uint64_t Get(const std::list<T, Alloc>& l) noexcept {
        return static_cast<uint64_t>(l.size()) * (sizeof(T) + sizeof(void*) * 2);
    }
};

template <typename T>
uint64_t GetContainerHeapSize(const T& container) {
    return HeapSizeTraits<T>::Get(container);
}

// Primary template: plain types have no heap data.
template <typename T>
constexpr bool HasHeapData = false;

template <typename T, typename Alloc>
constexpr bool HasHeapData<std::vector<T, Alloc>> = true;

template <typename CharT, typename Traits, typename Alloc>
constexpr bool HasHeapData<std::basic_string<CharT, Traits, Alloc>> = true;

template <typename K, typename V, typename Hash, typename Eq, typename Alloc>
constexpr bool HasHeapData<std::unordered_map<K, V, Hash, Eq, Alloc>> = true;

template <typename K, typename Hash, typename Eq, typename Alloc>
constexpr bool HasHeapData<std::unordered_set<K, Hash, Eq, Alloc>> = true;

template <typename K, typename V, typename Cmp, typename Alloc>
constexpr bool HasHeapData<std::map<K, V, Cmp, Alloc>> = true;

template <typename K, typename Cmp, typename Alloc>
constexpr bool HasHeapData<std::set<K, Cmp, Alloc>> = true;

template <typename T, typename Alloc>
constexpr bool HasHeapData<std::deque<T, Alloc>> = true;

template <typename T, typename Alloc>
constexpr bool HasHeapData<std::list<T, Alloc>> = true;

// Detect std::basic_string specializations for type tagging.
template <typename T>
struct IsStringLike : std::false_type {};

template <typename CharT, typename Traits, typename Alloc>
struct IsStringLike<std::basic_string<CharT, Traits, Alloc>> : std::true_type {};

} // namespace SpDiag

#endif // SPDIAG_ENABLE_MEMPOINT
