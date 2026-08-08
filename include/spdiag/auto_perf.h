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
#include "spdiag/perf_point.h"
#ifndef SPDIAG_DISABLE
#include "spdiag/perf_manager.h"
#endif

#ifndef SPDIAG_PERF_DEF_FILE
#define SPDIAG_PERF_DEF_FILE "perf_points.def"
#endif

#ifndef SPDIAG_PROGRAM_NAME
#define SPDIAG_PROGRAM_NAME nullptr
#endif

enum class PerfKey : uint32_t {
    NONE = 0,
#define PERF_KEY_DEF(e, m, p) e,
#include SPDIAG_PERF_DEF_FILE
#undef PERF_KEY_DEF
    COUNT
};

#ifndef SPDIAG_DISABLE
namespace {
    static const char* _spdiag_point_names[] = {
    #define PERF_KEY_DEF(e, m, p) p,
    #include SPDIAG_PERF_DEF_FILE
    #undef PERF_KEY_DEF
    };
    static const char* _spdiag_module_names[] = {
    #define PERF_KEY_DEF(e, m, p) m,
    #include SPDIAG_PERF_DEF_FILE
    #undef PERF_KEY_DEF
    };

#if defined(__GNUC__) || defined(__clang__)
    __attribute__((used))
#endif
    static const bool _spdiag_auto_init_ = []() {
        SpDiag::detail::AutoSlotCount() = static_cast<uint32_t>(PerfKey::COUNT);
        SpDiag::detail::AutoProgramName() = SPDIAG_PROGRAM_NAME;
        SpDiag::detail::AutoPointNames() = _spdiag_point_names;
        SpDiag::detail::AutoModuleNames() = _spdiag_module_names;
        return true;
    }();
}
#endif
