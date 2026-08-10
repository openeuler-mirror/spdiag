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
#include <vector>
#include <cstdint>
#include <unordered_map>
#include "text_table.h"
#include "format_util.h"
#include "runtime/data_pipeline.h"

namespace SpDiag {
namespace Cli {

struct CacheDisplayState {
    std::unordered_map<std::string, uint64_t> cumRef;
    std::unordered_map<std::string, uint64_t> cumMiss;
    uint64_t cumInstructions = 0;
    double avgIpc = 0.0;
    double avgFreqGhz = 0.0;
    double avgGips = 0.0;
    bool hasAvg = false;
};

void ShowObCacheDisplay(
    const std::vector<ProcessorOutput>& outputs,
    bool cumulative,
    CacheDisplayState& state);

}  // namespace Cli
}  // namespace SpDiag
