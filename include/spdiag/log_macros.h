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

#include "spdiag/log_config.h"
#include "spdiag/logger.h"

#define SPDIAG_LOG_LEVEL_DEBUG 0
#define SPDIAG_LOG_LEVEL_INFO  1
#define SPDIAG_LOG_LEVEL_WARN  2
#define SPDIAG_LOG_LEVEL_ERROR 3

// 编译时不过滤，所有级别日志均编译进二进制。
// 运行时通过 LoggerCore::SetRuntimeLogLevel() 动态控制日志输出级别。
#define SPDIAG_LOG(level, module, fmt, ...) \
    do { \
        SpDiag::LoggerCore::GetInstance().Log( \
            static_cast<SpDiag::LogLevel>(level), \
            static_cast<SpDiag::LogModule>(module), \
            fmt, \
            __FILE__, \
            __LINE__, \
            __FUNCTION__, \
            ##__VA_ARGS__); \
    } while(0)

#define SPDIAG_LOG_WITH_STACK(level, module, fmt, ...) \
    do { \
        SpDiag::LoggerCore::GetInstance().LogWithStack( \
            static_cast<SpDiag::LogLevel>(level), \
            static_cast<SpDiag::LogModule>(module), \
            fmt, \
            __FILE__, \
            __LINE__, \
            __FUNCTION__, \
            ##__VA_ARGS__); \
    } while(0)

#define SPDIAG_LOG_DEBUG(module, fmt, ...) \
    SPDIAG_LOG(SPDIAG_LOG_LEVEL_DEBUG, module, fmt, ##__VA_ARGS__)

#define SPDIAG_LOG_INFO(module, fmt, ...) \
    SPDIAG_LOG(SPDIAG_LOG_LEVEL_INFO, module, fmt, ##__VA_ARGS__)

#define SPDIAG_LOG_WARN(module, fmt, ...) \
    SPDIAG_LOG(SPDIAG_LOG_LEVEL_WARN, module, fmt, ##__VA_ARGS__)

#define SPDIAG_LOG_ERROR(module, fmt, ...) \
    SPDIAG_LOG(SPDIAG_LOG_LEVEL_ERROR, module, fmt, ##__VA_ARGS__)

#define SPDIAG_LOG_ERROR_WITH_STACK(module, fmt, ...) \
    SPDIAG_LOG_WITH_STACK(SPDIAG_LOG_LEVEL_ERROR, module, fmt, ##__VA_ARGS__)

#define SPDIAG_FLUSH_LOGS() \
    SpDiag::LoggerCore::GetInstance().FlushAll()

#define SPDIAG_SHUTDOWN_LOGS() \
    SpDiag::LoggerCore::GetInstance().Shutdown()
