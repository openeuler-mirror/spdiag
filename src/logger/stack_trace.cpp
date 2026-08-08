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

#include "stack_trace.h"

#include <execinfo.h>
#include <cxxabi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace SpDiag {

std::string CaptureStackTrace(int skip_frames) {
    const int max_frames = 64;
    void* frames[max_frames];
    int frame_count = backtrace(frames, max_frames);

    if (frame_count <= skip_frames) {
        return "";
    }

    char** symbols = backtrace_symbols(frames, frame_count);
    if (!symbols) {
        return "";
    }

    std::string result = "Stack trace:\n";
    char buffer[1024];

    for (int i = skip_frames; i < frame_count; ++i) {
        char* symbol = symbols[i];
        char* open_paren = strchr(symbol, '(');
        char* plus_sign = strchr(symbol, '+');
        char* close_paren = strchr(symbol, ')');

        int frame_idx = i - skip_frames;

        if (open_paren && plus_sign && close_paren && open_paren < plus_sign && plus_sign < close_paren) {
            // 提取偏移量部分（+后面的内容）
            char* offset_start = plus_sign + 1;
            char offset_buf[32] = "";
            size_t offset_len = close_paren - offset_start;
            if (offset_len > 0 && offset_len < sizeof(offset_buf)) {
                strncpy(offset_buf, offset_start, offset_len);
                offset_buf[offset_len] = '\0';
            }

            // 提取函数名
            *plus_sign = '\0';
            char* mangled_name = open_paren + 1;

            int status = 0;
            char* demangled = abi::__cxa_demangle(mangled_name, nullptr, nullptr, &status);

            if (status == 0 && demangled) {
                snprintf(buffer, sizeof(buffer), "#%d %s+%s", frame_idx, demangled, offset_buf);
                free(demangled);
            } else {
                snprintf(buffer, sizeof(buffer), "#%d %s+%s", frame_idx, mangled_name, offset_buf);
            }
        } else {
            // 无法解析符号，直接输出原始信息
            snprintf(buffer, sizeof(buffer), "#%d %s", frame_idx, symbol);
        }

        result += buffer;
        if (i < frame_count - 1) {
            result += "\n";
        }
    }

    free(symbols);
    return result;
}

} // namespace SpDiag