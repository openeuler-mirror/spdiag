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

#include "spdiag/mem_point_keys.h"

#include <cstdint>
#include <cstddef>

namespace SpDiag {

class MemPoint {
public:
    static MemPoint TrackValue(MemPointKey key,
                               const void* addr, uint64_t size) noexcept;

    template <typename T>
    static MemPoint TrackObject(MemPointKey key, const T& obj,
                                uint64_t extraBytes = 0) noexcept;

    static MemPoint TrackBuffer(MemPointKey key,
                                const void* buf, uint64_t size) noexcept;

    void UpdateSize(uint64_t newSize) noexcept;

    MemPoint(const MemPoint&) = delete;
    MemPoint& operator=(const MemPoint&) = delete;

    MemPoint(MemPoint&& other) noexcept;
    MemPoint& operator=(MemPoint&& other) noexcept;

    ~MemPoint() noexcept;

private:
    MemPoint(MemPointKey key, const void* obj, uint64_t size) noexcept;

    uint32_t    id_;
    uint64_t    size_;
    const void* obj_;
    bool        active_;

#ifdef SPDIAG_TEST
public:
    bool TestIsActive() const noexcept { return active_; }
    uint32_t TestGetId() const noexcept { return id_; }
    uint64_t TestGetSize() const noexcept { return size_; }
    const void* TestGetObj() const noexcept { return obj_; }
#endif
};

template <typename T>
MemPoint MemPoint::TrackObject(MemPointKey key, const T& obj,
                               uint64_t extraBytes) noexcept {
    return MemPoint(key, &obj, sizeof(T) + extraBytes);
}

} // namespace SpDiag

#endif // SPDIAG_ENABLE_MEMPOINT
