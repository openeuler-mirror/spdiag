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

#ifdef SPDIAG_ENABLE_MEMPOINT

#include "spdiag/mem_point.h"

#include <sys/sdt.h>

namespace SpDiag {

MemPoint::MemPoint(MemPointKey key, const void* obj, uint64_t size) noexcept
    : id_(static_cast<uint32_t>(key)),
      size_(size),
      obj_(obj),
      active_(true) {
    DTRACE_PROBE3(ob_mempoint, enter, id_, size_,
                  reinterpret_cast<uint64_t>(obj_));
}

MemPoint::~MemPoint() noexcept {
    if (active_) {
        DTRACE_PROBE3(ob_mempoint, leave, id_, size_,
                      reinterpret_cast<uint64_t>(obj_));
    }
}

MemPoint::MemPoint(MemPoint&& other) noexcept
    : id_(other.id_),
      size_(other.size_),
      obj_(other.obj_),
      active_(other.active_) {
    other.active_ = false;
}

MemPoint& MemPoint::operator=(MemPoint&& other) noexcept {
    if (this != &other) {
        if (active_) {
            DTRACE_PROBE3(ob_mempoint, leave, id_, size_,
                          reinterpret_cast<uint64_t>(obj_));
        }
        id_ = other.id_;
        size_ = other.size_;
        obj_ = other.obj_;
        active_ = other.active_;
        other.active_ = false;
    }
    return *this;
}

void MemPoint::UpdateSize(uint64_t newSize) noexcept {
    int64_t delta = static_cast<int64_t>(newSize) - static_cast<int64_t>(size_);
    size_ = newSize;
    DTRACE_PROBE3(ob_mempoint, resize, id_, static_cast<uint64_t>(delta),
                  reinterpret_cast<uint64_t>(obj_));
}

MemPoint MemPoint::TrackValue(MemPointKey key, const void* addr,
                              uint64_t size) noexcept {
    return MemPoint(key, addr, size);
}

MemPoint MemPoint::TrackBuffer(MemPointKey key, const void* buf,
                               uint64_t size) noexcept {
    return MemPoint(key, buf, size);
}

} // namespace SpDiag

#endif // SPDIAG_ENABLE_MEMPOINT
