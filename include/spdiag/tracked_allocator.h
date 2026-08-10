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

#include <cstddef>
#include <cstdint>
#include <new>
#include <sys/sdt.h>

#include "spdiag/mem_point_keys.h"

namespace SpDiag {

template <typename T, MemPointKey K>
class TrackedAllocator {
public:
    using value_type = T;
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap            = std::true_type;
    using is_always_equal                        = std::true_type;

    constexpr TrackedAllocator() noexcept = default;

    template <typename U>
    constexpr TrackedAllocator(const TrackedAllocator<U, K>&) noexcept {}

    template <typename U>
    struct rebind { using other = TrackedAllocator<U, K>; };

    [[nodiscard]] T* allocate(std::size_t n) {
        const std::size_t bytes = n * sizeof(T);
        T* p = static_cast<T*>(::operator new(bytes));
        DTRACE_PROBE3(ob_mempoint, enter,
                      static_cast<uint32_t>(K),
                      static_cast<uint64_t>(bytes),
                      reinterpret_cast<uint64_t>(p));
        return p;
    }

    void deallocate(T* p, std::size_t n) noexcept {
        if (p) {
            DTRACE_PROBE3(ob_mempoint, leave,
                          static_cast<uint32_t>(K),
                          static_cast<uint64_t>(n * sizeof(T)),
                          reinterpret_cast<uint64_t>(p));
            ::operator delete(static_cast<void*>(p));
        }
    }
};

template <typename T, typename U, MemPointKey K>
constexpr bool operator==(const TrackedAllocator<T, K>&,
                          const TrackedAllocator<U, K>&) noexcept {
    return true;
}

} // namespace SpDiag

#endif // SPDIAG_ENABLE_MEMPOINT
