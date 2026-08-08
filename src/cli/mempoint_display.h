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
#include "format_util.h"
#include "sort_spec.h"
#include "sort_engine.h"
#include "glob_match.h"
#include "filter_util.h"
#include "text_table.h"
#include "memstat_display.h"
#include "runtime/data_pipeline.h"

namespace SpDiag {
namespace Cli {

struct FieldEntry {
    std::string name;
    uint64_t staticSize = 0;
    uint64_t heapSize = 0;
    uint8_t  type = 0;  // 0=plain, 1=container, 2=string
};

struct MemPointEntry {
    int32_t pid = 0;
    uint32_t id = 0;
    std::string keyName;
    std::string category;
    uint64_t currentBytes = 0;
    uint64_t totalBytes = 0;
    uint64_t allocs = 0;
    uint64_t frees = 0;
    uint64_t peakBytes = 0;
    uint64_t maxSize = 0;
    uint64_t minSize = 0;
    uint64_t firstSeenNs = 0;
    uint64_t leakCount = 0;
    uint64_t leakMaxAgeNs = 0;
    bool hasData = false;
    uint64_t churnBytes = 0;
    double sessionDurationSec = 0.0;
    std::string structTypeName;
    std::vector<FieldEntry> fields;
    uint64_t totalStatic = 0;
    uint64_t totalHeap = 0;
    uint64_t paddingBytes = 0;
};

std::vector<MemPointEntry> ParseMemPointEntries(
    const std::vector<ProcessorOutput>& outputs);

void ShowMemPointDisplay(
    std::vector<MemPointEntry>& entries,
    bool humanReadable, int top,
    const std::vector<SortKey>& sortKeys,
    const std::vector<std::string>& filterGlobs,
    const std::string& showFields);

void ShowStructBreakdown(
    std::vector<MemPointEntry>& entries,
    bool humanReadable);

}  // namespace Cli
}  // namespace SpDiag
