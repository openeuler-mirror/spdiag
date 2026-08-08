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
#include <string>
#include <cstdint>
#include "spdiag/perf_level.h"

namespace SpDiag {

struct PerfConfig {
    std::string shmName         = "/spdiag_shm_default";
    RunMode     mode            = RunMode::DEBUG;
    uint8_t     levelGate       = 0;
    uint32_t    maxSlots        = 64;
    uint32_t    maxPrograms     = 8;
    uint32_t    dumpIntervalMs  = 0;
    std::string programName;
    uint32_t    slotCount       = 0;       // 用户自定义性能点数量（0=使用库默认值）
    const char* defFilePath     = nullptr; // 运行期 def 文件路径（用于加载名称映射）
};

}
