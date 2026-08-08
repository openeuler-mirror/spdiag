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

// ============================================================
// 编译期默认性能点定义（向后兼容，库内部使用）
// 用户可在自己项目中定义独立的枚举，通过 PerfPoint 的模板构造函数使用
// ============================================================

enum class PerfKeyDefined : uint32_t {
    NONE = 0,
#define PERF_KEY_DEF(enumName, moduleName, pointName) enumName,
#include "spdiag/perf_point.def"
#undef PERF_KEY_DEF
    PERF_KEY_DEFINED_COUNT
};

inline constexpr const char* perfKeyDefinedModules[] = {
#define PERF_KEY_DEF(enumName, moduleName, pointName) moduleName,
#include "spdiag/perf_point.def"
#undef PERF_KEY_DEF
};

inline constexpr const char* perfKeyDefinedPointNames[] = {
#define PERF_KEY_DEF(enumName, moduleName, pointName) pointName,
#include "spdiag/perf_point.def"
#undef PERF_KEY_DEF
};

}
