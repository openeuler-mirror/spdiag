/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
 * SpDiag is licensed under Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *         http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#pragma once

#include "spdiag/logger.h"

#include <cstring>
#include <iostream>
#include <string>

namespace SpDiag {

/**
 * @file log_cli_args.h
 * @brief Examples 内部公用的 --log-level 命令行参数解析工具。
 *
 * 定位说明：
 *   本文件仅供 examples/ 目录下的 demo/benchmark 程序使用，
 *   不属于 spdiag 库的公共 API（不安装、不导出）。
 *
 * 不放入 include/spdiag/ 的原因：
 *   - spdiag 主 CLI (src/cli/) 使用两阶段解析（ScanGlobalOptions + ParseArgsInternal），
 *     支持完整的命令体系（SHOW/WATCH/START...）、--pid/--libc-path 等数十个选项，
 *     其解析逻辑与 examples 中独立应用只需要 --log-level 的场景完全不同。
 *   - 库的公共头文件提供 ParseLogLevelName() 用于字符串→枚举的转换，
 *     本文件仅是对 argv 遍历的薄封装，examples 中 3 个程序共用此实现以避免重复代码，
 *     没有必要提升为库级公共接口。
 */

/**
 * 解析命令行中的 --log-level 参数。
 *
 * 支持的格式：
 *   --log-level LEVEL   空格分隔
 *   --log-level=LEVEL   等号分隔
 *
 * 说明：与 spdiag 主 CLI 入口 (src/cli/main.cpp) 保持一致，仅支持
 *       --log-level 长选项，不提供 -l 短选项。
 *
 * @param argc  参数数量
 * @param argv  参数数组
 * @param default_level  默认级别（未指定时使用）
 * @param out_level   输出：解析后的级别
 * @return true 解析成功；false 参数无效（out_level 保持默认值并输出警告）
 */
inline bool ParseLogLevelArg(int argc, char* argv[],
                             LogLevel default_level,
                             LogLevel& out_level) {
    out_level = default_level;
    bool found = false;

    for (int i = 1; i < argc; ++i) {
        bool is_level_flag = false;
        const char* value = nullptr;

        if (std::strcmp(argv[i], "--log-level") == 0 && i + 1 < argc) {
            is_level_flag = true;
            value = argv[++i];
        } else if (std::strncmp(argv[i], "--log-level=", 12) == 0) {
            is_level_flag = true;
            value = argv[i] + 12;
        }

        if (is_level_flag) {
            auto parsed = ParseLogLevelName(value);
            if (parsed.has_value()) {
                out_level = parsed.value();
                found = true;
            } else {
                std::cerr << "[WARN] Invalid log level: '" << value
                          << "'. Valid values: DEBUG, INFO, WARN, ERROR. "
                          << "Using default: "
                          << GetLogLevelName(default_level) << "\n";
            }
        }
    }

    return found;
}

/**
 * 帮助文本中的 --log-level 选项说明。
 */
inline void PrintLogLevelHelp() {
    std::cout
        << "  --log-level <LEVEL> Set runtime log level: DEBUG/INFO/WARN/ERROR\n"
        << "  --log-level=<LEVEL> (default: ERROR)\n";
}

} // namespace SpDiag
