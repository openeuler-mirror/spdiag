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

#include <cstdint>

namespace SpDiag {

enum class MemPointKey : uint32_t {
    NONE = 0,
#define MEM_KEY_DEF(enumName, category, description) enumName,
#include "spdiag/ob_mempoint_keys.def"
#undef MEM_KEY_DEF
    MEM_POINT_KEY_COUNT
};

inline constexpr const char* memPointKeyCategories[] = {
#define MEM_KEY_DEF(enumName, category, description) category,
#include "spdiag/ob_mempoint_keys.def"
#undef MEM_KEY_DEF
};

inline constexpr const char* memPointKeyDescriptions[] = {
#define MEM_KEY_DEF(enumName, category, description) description,
#include "spdiag/ob_mempoint_keys.def"
#undef MEM_KEY_DEF
};

inline constexpr uint32_t MemPointKeyCount() {
    return static_cast<uint32_t>(MemPointKey::MEM_POINT_KEY_COUNT) - 1;
}

inline const char* GetMemPointKeyCategory(MemPointKey key) {
    auto idx = static_cast<uint32_t>(key);
    if (idx == 0 || idx >= static_cast<uint32_t>(MemPointKey::MEM_POINT_KEY_COUNT)) {
        return "unknown";
    }
    return memPointKeyCategories[idx - 1];
}

inline const char* GetMemPointKeyDescription(MemPointKey key) {
    auto idx = static_cast<uint32_t>(key);
    if (idx == 0 || idx >= static_cast<uint32_t>(MemPointKey::MEM_POINT_KEY_COUNT)) {
        return "";
    }
    return memPointKeyDescriptions[idx - 1];
}

} // namespace SpDiag
