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
#include "async_worker.h"

#include <mutex>
#include <memory>
#include <vector>
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <cctype>
#include <thread>
#include <array>
#include <cstring>
#include <cstdlib>
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
    // 模块名表：SPDIAG_LOG_MODULE_LIST 展开后追加 "UNKNOWN"。
    // 注意 LogModule::UNKNOWN=255 是哨兵值，不在此表内，由下方 index 检查兜底。
    #define SPDIAG_LOG_MODULE_NAME(name) 1 +
    constexpr size_t kModuleCount = SPDIAG_LOG_MODULE_LIST(SPDIAG_LOG_MODULE_NAME) 1;
    #undef SPDIAG_LOG_MODULE_NAME
    static_assert(kModuleCount == 8, "names 表大小需与 SPDIAG_LOG_MODULE_LIST 匹配");

    #define SPDIAG_LOG_MODULE_NAME(name) #name,
    static const std::array<const char*, kModuleCount> names = {{
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

std::optional<LogLevel> ParseLogLevelName(const std::string& name) {
    // 转大写比较
    std::string upper;
    upper.reserve(name.size());
    for (char c : name) {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    if (upper == "DEBUG") return LogLevel::DEBUG;
    if (upper == "INFO")  return LogLevel::INFO;
    if (upper == "WARN")  return LogLevel::WARN;
    if (upper == "ERROR") return LogLevel::ERROR;
    return std::nullopt;
}

LoggerCore::LoggerCore() {
    if (SPDIAG_LOG_TO_CONSOLE) {
        sinks_snapshot_.push_back(std::make_shared<ConsoleSink>());
    }

    if (SPDIAG_LOG_TO_FILE) {
        sinks_snapshot_.push_back(std::make_shared<FileSink>(
            SPDIAG_LOG_FILE_PATH,
            SPDIAG_LOG_MAX_SIZE,
            SPDIAG_LOG_MAX_FILES
        ));
    }

    async_worker_ = std::make_shared<AsyncLogWorker>(
        SPDIAG_ASYNC_QUEUE_SIZE,
        [this]() -> AsyncLogWorker::SinkList {
            std::shared_lock<std::shared_mutex> lock(sinks_mutex_);
            return sinks_snapshot_;
        });
    async_worker_->Start();

    // 程序退出时自动刷盘并 join 后台线程，避免常驻单例（永不析构）导致
    // 退出时队列中未消费消息、以及 FileSink 缓冲未落盘内容静默丢失。
    // atexit 处理函数在静态对象析构阶段之前执行；单例为堆分配永不析构，
    // Shutdown() 只访问本单例持有的对象，即使部分静态对象已析构也无生命周期风险。
    // Shutdown() 幂等：若业务已显式调用 SPDIAG_SHUTDOWN_LOGS()，此处安全返回。
    std::atexit([]() {
        LoggerCore::GetInstance().Shutdown();
    });
}

LoggerCore::~LoggerCore() {
    Shutdown();
}

void LoggerCore::AddSink(std::unique_ptr<LogSink> sink) {
    if (!sink || shutdown_.load()) {
        return;
    }
    std::unique_lock<std::shared_mutex> lock(sinks_mutex_);
    if (shutdown_.load()) {
        return;
    }
    sinks_snapshot_.push_back(std::shared_ptr<LogSink>(std::move(sink)));
}

void LoggerCore::ClearSinks() {
    if (shutdown_.load()) {
        return;
    }
    // 先排空队列，确保所有已入队消息在清空 sinks 前被处理
    // 避免消息丢失：当 WorkerLoop 正在 batch 处理时，如果此时
    // get_sinks_() 返回空快照，batch 中的消息会被静默丢弃
    FlushAll();
    // FlushAll 完成后再获取锁，减少持锁时间
    std::unique_lock<std::shared_mutex> lock(sinks_mutex_);
    if (shutdown_.load()) {
        return;
    }
    sinks_snapshot_.clear();
}

size_t LoggerCore::GetDroppedCount() const {
    std::shared_ptr<AsyncLogWorker> worker;
    {
        std::shared_lock<std::shared_mutex> lock(worker_mutex_);
        worker = async_worker_;
    }
    if (worker) {
        return worker->DroppedCount();
    }
    return 0;
}

void LoggerCore::FlushAll() {
    if (shutdown_.load()) {
        return;
    }
    std::shared_ptr<AsyncLogWorker> worker;
    {
        std::shared_lock<std::shared_mutex> lock(worker_mutex_);
        worker = async_worker_;
    }
    if (worker) {
        worker->FlushSync();
    }
}

void LoggerCore::Shutdown() {
    if (shutdown_.exchange(true)) {
        return;
    }
    std::shared_ptr<AsyncLogWorker> worker;
    {
        std::unique_lock<std::shared_mutex> lock(worker_mutex_);
        worker = std::move(async_worker_);
    }
    if (worker) {
        worker->StopAndFlush();
    }
    std::unique_lock<std::shared_mutex> lock(sinks_mutex_);
    // StopAndFlush() 已在后台线程中完成所有 sink->Flush() 调用，
    // 此处无需再调用，避免并发访问风险。
    sinks_snapshot_.clear();
}

void LoggerCore::Log(LogLevel level, LogModule module, const char* fmt,
                     const char* file, int line, const char* func, ...) {
    if (shutdown_.load()) {
        return;
    }

    // 运行时级别过滤：atomic relaxed load + branch，约 1-2ns。
    if (level < runtime_level_.load(std::memory_order_relaxed)) {
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

    // 运行时级别过滤
    if (level < runtime_level_.load(std::memory_order_relaxed)) {
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
    // worker_mutex_ 仅在 Shutdown() 时被 unique_lock 持有（一次性操作），
    // 正常运行期无写者，shared_lock 为无竞争原子级开销（~1-2ns），
    // 且 AddSink/ClearSinks 不持有 worker_mutex_，不会阻塞日志热路径。
    std::shared_ptr<AsyncLogWorker> worker;
    {
        std::shared_lock<std::shared_mutex> lock(worker_mutex_);
        worker = async_worker_;
    }
    if (worker) {
        worker->Enqueue(std::move(formatted));
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

void LoggerCore::SetRuntimeLogLevel(LogLevel level) {
    runtime_level_.store(level, std::memory_order_relaxed);
}

LogLevel LoggerCore::GetRuntimeLogLevel() const {
    return runtime_level_.load(std::memory_order_relaxed);
}

} // namespace SpDiag
