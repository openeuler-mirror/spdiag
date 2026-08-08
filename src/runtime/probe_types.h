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
#include <string>

namespace SpDiag {

enum class ProbeType {
    UPROBE_ENTRY,
    UPROBE_RETURN,
    TRACEPOINT,
    SOFTWARE
};

using ProbeHandle = uint64_t;

struct ProbeDesc {
    ProbeType type = ProbeType::SOFTWARE;
    std::string targetBinary;
    std::string symbolName;
    std::string tracepointCategory;
    std::string tracepointName;
    int32_t targetPid = -1;
    uint64_t offset = 0;
    std::string ownerPlugin;
};

}
