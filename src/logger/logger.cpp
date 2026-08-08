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

#include "spdiag/logger.h"
#include "spdiag/log_config.h"
#include "sinks.h"
#include "stack_trace.h"

#include <mutex>
#include <memory>
#include <vector>
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <thread>
#include <array>
#include <cstring>
#include <chrono>

namespace SpDiag {

namespace {

constexpr const char kTruncMarker[] = "...[TRUNCATED]";
constexpr size_t kTruncMarkerLen = sizeof(kTruncMarker) - 1;

void MarkTruncated(char* buf, size_t buf_size) {
    if (buf_size <= kTruncMarkerLen + 1) {
        return;
    }
    size_t copy_pos = buf_size - kTruncMarkerLen - 1;
    std::memcpy(buf + copy_pos, kTruncMarker, kTruncMarkerLen + 1);
}

} // anonymous namespace

const char* GetLogModuleName(LogModule module) {
    #define SPDIAG_LOG_MODULE_NAME(name) #name,
    static const std::array<const char*, 8> names = {{
        SPDIAG_LOG_MODULE_LIST(SPDIAG_LOG_MODULE_NAME)
        "UNKNOWN"
    }};
    #undef SPDIAG_LOG_MODULE_NAME

    uint8_t index = static_cast<uint8_t>(module);
    if (index >= names.size()) {
        return "UNKNOWN";
    }
    return names[index];
}

const char* GetLogLevelName(LogLevel level) {
    static const std::array<const char*, 4> names = {{
        "DEBUG",
        "INFO",
        "WARN",
        "ERROR"
    }};

    uint8_t index = static_cast<uint8_t>(level);
    if (index >= names.size()) {
        return "UNKNOWN";
    }
    return names[index];
}

LoggerCore::LoggerCore() {
    if (SPDIAG_LOG_TO_CONSOLE) {
        sinks_.push_back(std::make_unique<ConsoleSink>());
    }

    if (SPDIAG_LOG_TO_FILE) {
        sinks_.push_back(std::make_unique<FileSink>(
            SPDIAG_LOG_FILE_PATH,
            SPDIAG_LOG_MAX_SIZE,
            SPDIAG_LOG_MAX_FILES
        ));
    }
}

LoggerCore::~LoggerCore() {
    Shutdown();
}

void LoggerCore::AddSink(std::unique_ptr<LogSink> sink) {
    if (!sink || shutdown_.load()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_.load()) {
        return;
    }
    sinks_.push_back(std::move(sink));
}

void LoggerCore::ClearSinks() {
    if (shutdown_.load()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_.load()) {
        return;
    }
    sinks_.clear();
}

void LoggerCore::FlushAll() {
    if (shutdown_.load()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_.load()) {
        return;
    }
    for (auto& sink : sinks_) {
        sink->Flush();
    }
}

void LoggerCore::Shutdown() {
    if (shutdown_.exchange(true)) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& sink : sinks_) {
        sink->Flush();
    }
    sinks_.clear();
}

void LoggerCore::Log(LogLevel level, LogModule module, const char* fmt,
                     const char* file, int line, const char* func, ...) {
    if (shutdown_.load()) {
        return;
    }

    va_list args;
    va_start(args, func);
    char message[4096];
    int ret = vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    if (ret >= static_cast<int>(sizeof(message))) {
        MarkTruncated(message, sizeof(message));
    }

    std::string formatted = FormatMessage(level, module, message, file, line, func);
    LogInternal(std::move(formatted));
}

void LoggerCore::LogWithStack(LogLevel level, LogModule module, const char* fmt,
                             const char* file, int line, const char* func, ...) {
    if (shutdown_.load()) {
        return;
    }

    va_list args;
    va_start(args, func);
    char message[4096];
    int ret = vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    if (ret >= static_cast<int>(sizeof(message))) {
        MarkTruncated(message, sizeof(message));
    }

    std::string stack_trace = CaptureStackTrace();
    std::string formatted = FormatMessage(level, module, message, file, line, func, &stack_trace);
    LogInternal(std::move(formatted));
}

void LoggerCore::LogInternal(std::string&& formatted) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_.load()) {
        return;
    }
    for (auto& sink : sinks_) {
        sink->Write(formatted);
    }
}

std::string LoggerCore::FormatMessage(LogLevel level, LogModule module, const char* message,
                                      const char* file, int line, const char* func,
                                      const std::string* stack_trace) {
        char buffer[8192];
        char timestamp[64];

        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        struct tm local_tm;
        localtime_r(&time_t, &local_tm);
        snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                 local_tm.tm_year + 1900,
                 local_tm.tm_mon + 1,
                 local_tm.tm_mday,
                 local_tm.tm_hour,
                 local_tm.tm_min,
                 local_tm.tm_sec,
                 static_cast<int>(ms.count()));

        const char* slash = file;
        while (*file != '\0') {
            if (*file == '/' || *file == '\\') {
                slash = file + 1;
            }
            file++;
        }

        int ret;
        if (stack_trace && !stack_trace->empty()) {
            ret = snprintf(buffer, sizeof(buffer),
                           "[%s] [%s] [%s] [%s:%d] [%s] - %s\n%s",
                           timestamp,
                           GetLogLevelName(level),
                           GetLogModuleName(module),
                           slash,
                           line,
                           func,
                           message,
                           stack_trace->c_str());
        } else {
            ret = snprintf(buffer, sizeof(buffer),
                           "[%s] [%s] [%s] [%s:%d] [%s] - %s",
                           timestamp,
                           GetLogLevelName(level),
                           GetLogModuleName(module),
                           slash,
                           line,
                           func,
                           message);
        }

        if (ret >= static_cast<int>(sizeof(buffer))) {
            MarkTruncated(buffer, sizeof(buffer));
        }

        return std::string(buffer);
}

} // namespace SpDiag
