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

struct agg_key {
    uint32_t pid;
    uint32_t tid;
    int32_t stack_id;
};

struct agg_val {
    uint64_t current_bytes;
    uint64_t total_alloc;
    uint64_t alloc_count;
    uint64_t free_count;
    uint64_t peak;
    uint64_t max_size;
    uint64_t min_size;
    uint64_t first_seen_ns;
    uint64_t last_seen_ns;
};

struct FrameInfo {
    uint64_t ip = 0;
    std::string func;
    std::string module;
    std::string file;
    uint32_t line = 0;
};

}
