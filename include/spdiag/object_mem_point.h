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

#include "spdiag/mem_point.h"
#include "spdiag/mem_point_keys.h"
#include "spdiag/field_size_traits.h"

#include <sys/sdt.h>

#include <cstddef>
#include <cstring>
#include <functional>
#include <type_traits>

namespace SpDiag {

// ---------------------------------------------------------------------------
// Binary layout constants shared with the BPF program.
// Must use packed layout to guarantee consistent field offsets across the
// user-space / BPF boundary.
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct FieldSnapshot {
    char     name[16];    // null-terminated field name (max 15 chars + '\0')
    uint32_t staticSize;
    uint32_t heapSize;
    uint8_t  type;        // 0 = plain, 1 = container, 2 = string
    uint8_t  padding[3];
};

struct SnapshotBuffer {
    char     typeName[32];      // null-terminated struct/class name (max 31 + '\0')
    uint16_t numFields;
    uint16_t reserved;
    uint32_t totalStatic;
    uint32_t totalHeap;
    uint32_t paddingBytes;      // sizeof(T) - totalStatic
    FieldSnapshot fields[16];   // max 16 fields
};
#pragma pack(pop)

// ---------------------------------------------------------------------------
// ObjectMemPoint — per-field memory breakdown builder.
//
// Usage:
//   ObjectMemPoint<MyStruct> mp("MyStruct", MemPointKey::MY_KEY, obj);
//   mp.Field("count", &MyStruct::count)
//      .ContainerField("items", &MyStruct::items)
//      .Snapshot();
// ---------------------------------------------------------------------------
template <typename T>
class ObjectMemPoint {
public:
    // -- Construction --------------------------------------------------------

    ObjectMemPoint(const char* typeName, MemPointKey key, const T& obj)
        : memPoint_(MemPoint::TrackObject(key, obj)),
          id_(static_cast<uint32_t>(key)),
          obj_(&obj),
          numFields_(0) {
        // Store the type name, truncated to 31 characters.
        std::memset(typeName_, 0, sizeof(typeName_));
        if (typeName != nullptr) {
            const std::size_t sourceLength = std::strlen(typeName);
            const std::size_t copyLength =
                sourceLength < sizeof(typeName_) ? sourceLength : sizeof(typeName_) - 1;
            std::memcpy(typeName_, typeName, copyLength);
        }
    }

    // -- Move semantics ------------------------------------------------------

    ObjectMemPoint(ObjectMemPoint&& other) noexcept
        : memPoint_(std::move(other.memPoint_)),
          id_(other.id_),
          obj_(other.obj_),
          numFields_(other.numFields_) {
        std::memcpy(typeName_, other.typeName_, sizeof(typeName_));
        for (uint16_t i = 0; i < numFields_; ++i) {
            fields_[i] = std::move(other.fields_[i]);
        }
        other.numFields_ = 0;
    }

    ObjectMemPoint& operator=(ObjectMemPoint&& other) noexcept {
        if (this != &other) {
            memPoint_ = std::move(other.memPoint_);
            id_ = other.id_;
            obj_ = other.obj_;
            numFields_ = other.numFields_;
            std::memcpy(typeName_, other.typeName_, sizeof(typeName_));
            for (uint16_t i = 0; i < numFields_; ++i) {
                fields_[i] = std::move(other.fields_[i]);
            }
            other.numFields_ = 0;
        }
        return *this;
    }

    // Copy is deleted — MemPoint is non-copyable.
    ObjectMemPoint(const ObjectMemPoint&) = delete;
    ObjectMemPoint& operator=(const ObjectMemPoint&) = delete;

    // -- Destruction ---------------------------------------------------------

    ~ObjectMemPoint() noexcept = default;
    // ~memPoint_ fires the USDT leave probe.

    // -- Field registration (builder pattern, returns *this) -----------------

    // Plain field: static size only, no heap contribution.
    template <typename M>
    ObjectMemPoint& Field(const char* name, M T::* member) {
        if (numFields_ >= kMaxFields) {
            return *this;
        }
        auto& desc = fields_[numFields_++];
        desc.name = (name != nullptr) ? name : "";
        desc.staticSize = static_cast<uint32_t>(sizeof(M));
        desc.type = kTypePlain;
        desc.heapCalc = nullptr;
        // Suppress unused-parameter warning — member is captured only via
        // its type M for sizeof; the pointer value is not needed for plain
        // fields.
        (void)member;
        return *this;
    }

    // Container field: static size + heap size via GetContainerHeapSize.
    template <typename C>
    ObjectMemPoint& ContainerField(const char* name, C T::* member) {
        if (numFields_ >= kMaxFields) {
            return *this;
        }
        auto& desc = fields_[numFields_++];
        desc.name = (name != nullptr) ? name : "";
        desc.staticSize = static_cast<uint32_t>(sizeof(C));

        // std::string gets its own type tag for BPF-side parsing.
        // Decay cv-qualifications so std::string matches cleanly.
        using CleanC = std::decay_t<C>;
        desc.type = IsStringLike<CleanC>::value ? kTypeString : kTypeContainer;

        // Capture the member pointer by value in the lambda.  A pointer-to-
        // data-member is typically 4-8 bytes, well within the small-buffer
        // optimisation of std::function — no heap allocation.
        desc.heapCalc = [member](const T& obj) -> uint64_t {
            return GetContainerHeapSize(obj.*member);
        };
        return *this;
    }

    // -- Snapshot ------------------------------------------------------------

    void Snapshot() {
        SnapshotBuffer buf{};
        std::memcpy(buf.typeName, typeName_, sizeof(buf.typeName));

        uint32_t totalStatic = 0;
        uint32_t totalHeap   = 0;

        for (uint16_t i = 0; i < numFields_; ++i) {
            const auto& desc = fields_[i];
            auto& snap = buf.fields[i];

            // Copy field name, truncating to 15 characters.
            std::memset(snap.name, 0, sizeof(snap.name));
            std::strncpy(snap.name, desc.name, sizeof(snap.name) - 1);

            snap.staticSize = desc.staticSize;

            // Compute heap contribution.
            uint32_t heapSize = 0;
            if (desc.heapCalc != nullptr) {
                heapSize = static_cast<uint32_t>(desc.heapCalc(*obj_));
            }
            snap.heapSize = heapSize;
            snap.type     = desc.type;
            std::memset(snap.padding, 0, sizeof(snap.padding));

            totalStatic += desc.staticSize;
            totalHeap   += heapSize;
        }

        buf.numFields   = numFields_;
        buf.reserved    = 0;
        buf.totalStatic = totalStatic;
        buf.totalHeap   = totalHeap;
        buf.paddingBytes = static_cast<uint32_t>(
            sizeof(T) > totalStatic ? sizeof(T) - totalStatic : 0);

        const uint64_t bufLen = static_cast<uint64_t>(sizeof(SnapshotBuffer));
        DTRACE_PROBE3(ob_mempoint, struct_snapshot,
                      id_,
                      reinterpret_cast<uint64_t>(&buf),
                      bufLen);
    }

    // -- Test accessors (only compiled in test builds) -----------------------

#ifdef SPDIAG_TEST
    uint32_t    TestGetId() const noexcept { return id_; }
    const char* TestGetTypeName() const noexcept { return typeName_; }
    uint16_t    TestGetNumFields() const noexcept { return numFields_; }
    uint32_t    TestGetStaticSize(uint16_t idx) const noexcept {
        if (idx >= numFields_ || idx >= kMaxFields) {
            return 0;
        }
        return fields_[idx].staticSize;
    }
    const char* TestGetFieldName(uint16_t idx) const noexcept {
        if (idx >= numFields_ || idx >= kMaxFields) {
            return "";
        }
        return fields_[idx].name;
    }
    uint8_t     TestGetFieldType(uint16_t idx) const noexcept {
        if (idx >= numFields_ || idx >= kMaxFields) {
            return 0xFF;
        }
        return fields_[idx].type;
    }
    uint64_t    TestCalcHeapSize(uint16_t idx) const noexcept {
        if (idx >= numFields_ || idx >= kMaxFields ||
            fields_[idx].heapCalc == nullptr) {
            return 0;
        }
        return fields_[idx].heapCalc(*obj_);
    }
#endif

private:
    // Field type tags (must match BPF-side definitions).
    static constexpr uint8_t kTypePlain     = 0;
    static constexpr uint8_t kTypeContainer = 1;
    static constexpr uint8_t kTypeString    = 2;

    static constexpr uint16_t kMaxFields = 16;

    // Internal field descriptor.
    struct FieldDesc {
        const char* name = "";
        uint32_t    staticSize = 0;
        uint8_t     type = 0;
        std::function<uint64_t(const T&)> heapCalc;
    };

    char        typeName_[32];
    MemPoint    memPoint_;
    uint32_t    id_;
    const T*    obj_;
    uint16_t    numFields_;
    FieldDesc   fields_[kMaxFields];
};

} // namespace SpDiag

#endif // SPDIAG_ENABLE_MEMPOINT
