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

#include "spdiag/log_modules.h"
#include "spdiag/log_sink.h"

#include <cstdint>
#include <string>
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdarg>

namespace SpDiag {

enum class LogLevel : uint8_t {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3
};

#define SPDIAG_LOG_MODULE_ENUM(name) name,
enum class LogModule : uint8_t {
    SPDIAG_LOG_MODULE_LIST(SPDIAG_LOG_MODULE_ENUM)
    UNKNOWN = 255
};
#undef SPDIAG_LOG_MODULE_ENUM

const char* GetLogModuleName(LogModule module);
const char* GetLogLevelName(LogLevel level);

class LoggerCore {
public:
    static LoggerCore& GetInstance() {
        static LoggerCore instance;
        return instance;
    }

    ~LoggerCore();

    void Log(LogLevel level, LogModule module, const char* fmt,
        const char* file, int line, const char* func, ...);
    
    void LogWithStack(LogLevel level, LogModule module, const char* fmt,
        const char* file, int line, const char* func, ...);

    void AddSink(std::unique_ptr<LogSink> sink);

    void FlushAll();

    void Shutdown();

    void ClearSinks();

private:
    LoggerCore();
    LoggerCore(const LoggerCore&) = delete;
    LoggerCore& operator=(const LoggerCore&) = delete;

    std::string FormatMessage(LogLevel level, LogModule module, const char* message,
        const char* file, int line, const char* func,
        const std::string* stack_trace = nullptr);

    void LogInternal(std::string&& formatted);

    std::mutex mutex_;
    std::atomic<bool> shutdown_{false};
    std::vector<std::unique_ptr<LogSink>> sinks_;
};

} // namespace SpDiag
