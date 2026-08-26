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
#include <cstddef>
#include <string>
#include <memory>
#include <vector>
#include <shared_mutex>
#include <mutex>
#include <atomic>
#include <cstdarg>
#include <optional>

namespace SpDiag {

class AsyncLogWorker;  // 前向声明，内部实现类

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

/**
 * 将日志级别名称字符串解析为 LogLevel 枚举值。
 * 不区分大小写，支持 "DEBUG"/"INFO"/"WARN"/"ERROR"。
 *
 * @param name 级别名称字符串
 * @return 解析成功返回 LogLevel，失败返回 std::nullopt
 */
std::optional<LogLevel> ParseLogLevelName(const std::string& name);

class LoggerCore {
public:
    static LoggerCore& GetInstance() {
        // 常驻单例（堆分配、永不析构）。
        // 函数局部静态对象会在程序退出时析构；若其他全局/静态对象的析构函数
        // 中打日志，会访问已销毁的单例（未定义行为）。堆分配永不析构消除了
        // 该生命周期风险。退出时的刷盘与后台线程 join 由构造函数注册的
        // atexit 处理函数自动完成（调用 Shutdown()，幂等）；业务如需提前
        // 落盘，仍可显式调用 SPDIAG_SHUTDOWN_LOGS()（可安全重复调用）。
        static LoggerCore* instance = new LoggerCore();
        return *instance;
    }

    ~LoggerCore();

    void Log(LogLevel level, LogModule module, const char* fmt,
        const char* file, int line, const char* func, ...);
    
    void LogWithStack(LogLevel level, LogModule module, const char* fmt,
        const char* file, int line, const char* func, ...);

    /**
     * 注册一个新的日志输出目标。
     *
     * 线程安全：内部使用 unique_lock<shared_mutex> 保护 sinks_snapshot_，
     * 仅与后台线程的 get_sinks_()（持 shared_lock 读快照）存在写-读竞争。
     *
     * 性能注意：AddSink/ClearSinks 不阻塞业务日志热路径（Log/LogWithStack），
     *   因为日志热路径不再获取 sinks 锁；仅后台线程在下一次 batch 处理时
     *   短暂持有 shared_lock 读快照，写锁等待时间极短。建议仍在启动阶段
     *   完成 sink 注册，避免不必要的锁争用。
     */
    void AddSink(std::unique_ptr<LogSink> sink);

    void FlushAll();

    void Shutdown();

    /**
     * 清空所有已注册的 sinks。与 AddSink 共享相同的线程安全说明。
     */
    void ClearSinks();

    size_t GetDroppedCount() const;

    /**
     * 设置运行时日志级别。
     *
     * 所有日志级别均编译进二进制，通过此接口在运行时动态控制日志输出粒度。
     * 线程安全：使用 atomic<LogLevel> 存储，memory_order_relaxed 加载。
     * 建议在程序启动阶段调用一次，避免运行期间频繁修改。
     *
     * @param level 运行时日志级别
     */
    void SetRuntimeLogLevel(LogLevel level);

    /**
     * 获取当前运行时日志级别。
     * 线程安全，可在多线程环境中并发调用。
     */
    LogLevel GetRuntimeLogLevel() const;

private:
    LoggerCore();
    LoggerCore(const LoggerCore&) = delete;
    LoggerCore& operator=(const LoggerCore&) = delete;

    std::string FormatMessage(LogLevel level, LogModule module, const char* message,
        const char* file, int line, const char* func,
        const std::string* stack_trace = nullptr);

    void LogInternal(std::string&& formatted);

    // ERROR 同步路径：取 sinks 快照后逐 sink 同步 Write（可选 Flush）。
    // 与后台 worker 的批量写并发，由各 sink 内部互斥锁串行化。
    void WriteSync(std::string&& formatted);

    std::atomic<bool> shutdown_{false};

    // worker_mutex_: 保护 async_worker_ 指针的读写。
    // 写方：仅构造函数和 Shutdown()（一次性操作）。
    // 读方：每条日志的 LogInternal() 持 shared_lock 读取指针副本。
    // 由于正常运行期不存在写者，shared_lock 在无竞争下为原子级开销（~1-2ns），
    // 远低于一次日志格式化的开销。且 AddSink/ClearSinks 不再竞争此锁。
    mutable std::shared_mutex worker_mutex_;
    std::shared_ptr<AsyncLogWorker> async_worker_;

    // sinks_mutex_: 仅保护 sinks_snapshot_ 的读写。
    // 读方：后台线程每轮 batch 调用 get_sinks_() 时持 shared_lock。
    // 写方：AddSink/ClearSinks 持 unique_lock。
    // 日志热路径（Log/LogWithStack/LogInternal）完全不获取此锁，
    // 因此运行期动态增删 sinks 不会阻塞任何业务日志调用。
    mutable std::shared_mutex sinks_mutex_;
    std::vector<std::shared_ptr<LogSink>> sinks_snapshot_;

    // 运行时日志级别，初始化为 ERROR。
    // 所有日志级别均编译进二进制，通过 SetRuntimeLogLevel() 动态控制输出级别。
    // 使用 atomic + memory_order_relaxed 保证线程安全且开销最小。
    std::atomic<LogLevel> runtime_level_{LogLevel::ERROR};
};

} // namespace SpDiag
