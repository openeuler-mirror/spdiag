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

enum class PluginState {
    REGISTERED,
    INITIALIZED,
    RUNNING,
    STOPPED,
    DESTROYED
};

constexpr uint32_t FEAT_CUSTOM_UPROBE = 1U << 0;
constexpr uint32_t FEAT_CUSTOM_TRACEPOINT = 1U << 1;
constexpr uint32_t FEAT_OB_MEMORY = 1U << 2;
constexpr uint32_t FEAT_OB_CACHE = 1U << 3;
constexpr uint32_t FEAT_MEMPOINT = 1U << 4;

struct PluginResourceReq {
    uint32_t mapMemoryKb = 0;
    uint32_t maxProbes = 0;
    uint32_t maxChannels = 0;
};

inline bool HasFeature(uint32_t featureMask, uint32_t feature) {
    return (featureMask & feature) != 0;
}

}
