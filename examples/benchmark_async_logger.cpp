/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
 * SpDiag is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *         http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 */

// SpDiag 异步日志性能基准测试
//
// 核心目标：量化"每打 1 条日志在业务线程上增加的耗时"
//
// 测试场景：
//   0. NoOpBaseline          — 空函数调用基线（不打日志），作为"零开销"参照
//   1. SingleThreadEnqueue    — 单线程 Enqueue 纯延迟（业务线程路径，不等待 Flush）
//   2. SingleThreadEndToEnd   — 含 FlushAll 的端到端延迟（含后台刷盘时间）
//   3. MultiThreadThroughput  — 多线程并发吞吐（4/8/16/32 线程，模拟真实业务并发）
//   4. HighPressureDrop      — 慢盘模拟下的丢弃率（队列满时被丢弃的日志比例）
//   5. LatencyHistogram      — 分位延迟统计（P50/P99/P999/MAX + 分布直方图）
//   6. RuntimeFilterOverhead — 运行时日志级别过滤开销（vs 编译时过滤假设为0）
//
// 用法：
//   benchmark_async_logger [options]
//     -n <iterations>   每轮基准迭代次数（默认 100000）
//     -t <threads>      吞吐测试最大线程数（默认 32，会测试 4/8/16/32）
//     -s <slow-ms>      慢 Sink 模拟延迟（默认 2ms，用于丢弃场景）
//     -h / --help       显示帮助

#include "spdiag/logger.h"
#include "spdiag/log_macros.h"
#include "spdiag/log_sink.h"
#include "spdiag/log_config.h"
#include "log_cli_args.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

using namespace SpDiag;
using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// ANSI 颜色（终端美化）
// ---------------------------------------------------------------------------
namespace Color {
const char* RESET   = "\033[0m";
const char* BOLD    = "\033[1m";
const char* DIM     = "\033[2m";
const char* GREEN   = "\033[32m";
const char* YELLOW  = "\033[33m";
const char* CYAN    = "\033[36m";
const char* RED     = "\033[31m";
const char* MAGENTA = "\033[35m";
const char* BG_BLUE = "\033[44m";
} // namespace Color

// ---------------------------------------------------------------------------
// 工具 Sink
// ---------------------------------------------------------------------------

// 计数 Sink：仅统计，不做 I/O，隔离框架本身的开销
class CountingSink : public LogSink {
public:
    void Write(const std::string&) override { count_.fetch_add(1, std::memory_order_relaxed); }
    void Flush() override {}
    size_t Count() const { return count_.load(std::memory_order_relaxed); }
private:
    std::atomic<size_t> count_{0};
};

// 慢 Sink：模拟磁盘写入延迟
class SlowSink : public LogSink {
public:
    explicit SlowSink(std::chrono::milliseconds delay) : delay_(delay) {}
    void Write(const std::string&) override {
        std::this_thread::sleep_for(delay_);
        written_.fetch_add(1, std::memory_order_relaxed);
    }
    void Flush() override {}
    size_t Written() const { return written_.load(std::memory_order_relaxed); }
private:
    std::chrono::milliseconds delay_;
    std::atomic<size_t> written_{0};
};

// ---------------------------------------------------------------------------
// 辅助函数
// ---------------------------------------------------------------------------

struct BenchConfig {
    int iterations = 100000;
    int max_threads = 32;
    int slow_ms = 2;
};

// 重复 UTF-8 字符串 n 次（用于画表格线）
std::string RepeatStr(const std::string& s, int count) {
    std::string result;
    result.reserve(s.size() * count);
    for (int i = 0; i < count; ++i) result += s;
    return result;
}

// 表格横线字符 (U+2500, UTF-8: E2 94 80)
const std::string kTableHL = "─";

// 格式化时间：自动选择 ns/us/ms
std::string FormatTime(double ns) {
    if (ns < 1000.0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.1f ns", ns);
        return buf;
    } else if (ns < 1e6) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.2f us", ns / 1000.0);
        return buf;
    } else {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.2f ms", ns / 1e6);
        return buf;
    }
}

// 格式化吞吐：自动选择 /s /ks /Ms
std::string FormatThroughput(double per_sec) {
    char buf[64];
    if (per_sec < 1000.0) {
        std::snprintf(buf, sizeof(buf), "%.0f logs/s", per_sec);
    } else if (per_sec < 1e6) {
        std::snprintf(buf, sizeof(buf), "%.2f K logs/s", per_sec / 1000.0);
    } else {
        std::snprintf(buf, sizeof(buf), "%.2f M logs/s", per_sec / 1e6);
    }
    return buf;
}

void PrintHeader(const BenchConfig& cfg) {
    std::cout << Color::BOLD << Color::CYAN
              << "╔══════════════════════════════════════════════════════════════╗\n"
              << "║        SpDiag Async Logger Performance Benchmark            ║\n"
              << "╚══════════════════════════════════════════════════════════════╝\n"
              << Color::RESET;
    std::cout << Color::DIM
              << "  Queue Size:        " << SPDIAG_ASYNC_QUEUE_SIZE << "\n"
              << "  Enqueue Timeout:  " << SPDIAG_ASYNC_ENQUEUE_TIMEOUT_MS << " ms\n"
              << "  Runtime Level:    "
              << GetLogLevelName(LoggerCore::GetInstance().GetRuntimeLogLevel()) << "\n"
              << "  Benchmark Level:  same as runtime (always passes filter)\n"
              << "  Iterations:       " << cfg.iterations << "\n"
              << "  Max Threads:      " << cfg.max_threads << "\n"
              << "  Slow Sink Delay:  " << cfg.slow_ms << " ms\n"
              << Color::RESET << "\n";
}

void PrintDivider(const std::string& title) {
    std::cout << Color::BOLD << Color::CYAN
              << "┌──────────────────────────────────────────────────────────────┐\n"
              << "│ " << Color::YELLOW << std::left << std::setw(58) << title
              << Color::CYAN << "│\n"
              << "└──────────────────────────────────────────────────────────────┘"
              << Color::RESET << "\n";
}

// ---------------------------------------------------------------------------
// 场景 0：NoOp 基线 —— 空函数调用的耗时
// ---------------------------------------------------------------------------
void BenchNoOpBaseline(int iterations, double& baseline_ns) {
    PrintDivider("Scenario 0: NoOp Baseline (Zero-Call Overhead)");
    std::cout << Color::DIM
              << "  Measuring the overhead of calling an empty function.\n"
              << "  This is the 'zero-cost' baseline — compare logging\n"
              << "  overhead against this to get the true per-log cost.\n\n"
              << Color::RESET;

    // 空 lambda：什么都不做，仅测量函数调用本身的开销
    auto noop = [](int) { /* no-op */ };

    // Warm-up
    for (int i = 0; i < 1000; ++i) noop(i);

    std::vector<double> latencies_ns;
    latencies_ns.reserve(iterations);

    for (int i = 0; i < iterations; ++i) {
        auto t0 = Clock::now();
        noop(i);
        auto t1 = Clock::now();
        latencies_ns.push_back(
            std::chrono::duration<double, std::nano>(t1 - t0).count());
    }

    double total_ns = 0;
    for (double v : latencies_ns) total_ns += v;
    double avg_ns = total_ns / iterations;

    std::sort(latencies_ns.begin(), latencies_ns.end());
    size_t p50_idx = iterations * 50 / 100;
    size_t p99_idx = iterations * 99 / 100;
    size_t p999_idx = iterations * 999 / 1000;

    std::cout << Color::GREEN
              << "  Baseline (avg):    " << FormatTime(avg_ns) << "\n"
              << Color::RESET;
    std::cout << "  P50:               " << FormatTime(latencies_ns[p50_idx]) << "\n";
    std::cout << "  P99:               " << FormatTime(latencies_ns[p99_idx]) << "\n";
    std::cout << "  P999:              " << FormatTime(latencies_ns[p999_idx]) << "\n";
    std::cout << "  Max:               " << FormatTime(latencies_ns.back()) << "\n\n";

    baseline_ns = avg_ns;
}

// ---------------------------------------------------------------------------
// 场景 1：单线程 Enqueue 纯延迟
// ---------------------------------------------------------------------------
void BenchSingleThreadEnqueue(int iterations, double baseline_ns,
                              double& enqueue_avg_ns) {
    PrintDivider("Scenario 1: SingleThread Enqueue Latency");
    std::cout << Color::DIM
              << "  Measuring pure Enqueue latency on business thread.\n"
              << "  No FlushAll between calls — only enqueue+return path.\n\n"
              << Color::RESET;

    auto& logger = LoggerCore::GetInstance();
    logger.ClearSinks();
    auto sink = std::make_unique<CountingSink>();
    auto* sink_raw = sink.get();
    logger.AddSink(std::move(sink));

    LogLevel runtime_level = logger.GetRuntimeLogLevel();

    // Warm-up
    for (int i = 0; i < 1000; ++i) {
        SPDIAG_LOG(runtime_level, LogModule::SDK, "warmup %d", i);
    }
    logger.FlushAll();

    std::vector<double> latencies_ns;
    latencies_ns.reserve(iterations);

    for (int i = 0; i < iterations; ++i) {
        auto t0 = Clock::now();
        SPDIAG_LOG(runtime_level, LogModule::SDK, "bench msg %d", i);
        auto t1 = Clock::now();
        latencies_ns.push_back(
            std::chrono::duration<double, std::nano>(t1 - t0).count());
    }

    logger.FlushAll();

    double total_ns = 0;
    for (double v : latencies_ns) total_ns += v;
    double avg_ns = total_ns / iterations;

    std::sort(latencies_ns.begin(), latencies_ns.end());
    size_t p50_idx = iterations * 50 / 100;
    size_t p99_idx = iterations * 99 / 100;
    size_t p999_idx = iterations * 999 / 1000;

    std::cout << "  Messages received by sink: " << sink_raw->Count()
              << " / " << iterations << "\n\n";

    std::cout << Color::GREEN
              << "  Avg per log:       " << FormatTime(avg_ns) << "\n"
              << Color::RESET;

    // 计算日志本身的净开销
    double overhead_ns = avg_ns - baseline_ns;
    double overhead_pct = (avg_ns > 0) ? 100.0 * overhead_ns / avg_ns : 0.0;

    std::cout << Color::YELLOW
              << "  Net overhead:      " << FormatTime(overhead_ns)
              << " (" << std::fixed << std::setprecision(1)
              << overhead_pct << "% of total)\n"
              << Color::RESET;

    std::cout << "  P50:               " << FormatTime(latencies_ns[p50_idx]) << "\n";
    std::cout << "  P99:               " << FormatTime(latencies_ns[p99_idx]) << "\n";
    std::cout << "  P999:              " << FormatTime(latencies_ns[p999_idx]) << "\n";
    std::cout << "  Max:               " << FormatTime(latencies_ns.back()) << "\n\n";

    enqueue_avg_ns = avg_ns;

    logger.ClearSinks();
    logger.FlushAll();
}

// ---------------------------------------------------------------------------
// 场景 2：单线程端到端延迟（含 FlushAll）
// ---------------------------------------------------------------------------
void BenchSingleThreadEndToEnd(int iterations, double& e2e_total_s) {
    PrintDivider("Scenario 2: SingleThread End-to-End Latency");
    std::cout << Color::DIM
              << "  Measuring full path latency including FlushAll.\n"
              << "  End-to-end = format + enqueue + background write + FlushAll.\n\n"
              << Color::RESET;

    auto& logger = LoggerCore::GetInstance();
    logger.ClearSinks();
    logger.AddSink(std::make_unique<CountingSink>());

    LogLevel runtime_level = logger.GetRuntimeLogLevel();

    // Warm-up
    for (int i = 0; i < 1000; ++i) {
        SPDIAG_LOG(runtime_level, LogModule::SDK, "warmup %d", i);
    }
    logger.FlushAll();

    auto start = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        SPDIAG_LOG(runtime_level, LogModule::SDK, "bench msg %d", i);
    }
    logger.FlushAll();
    auto end = Clock::now();

    double elapsed = std::chrono::duration<double>(end - start).count();
    double throughput = (elapsed > 0) ? iterations / elapsed : 0;
    double avg_ns = (iterations > 0) ? (elapsed * 1e9) / iterations : 0;

    std::cout << Color::GREEN
              << "  Total time:        " << std::fixed << std::setprecision(4)
              << elapsed << " s\n"
              << Color::RESET;
    std::cout << "  Avg per log:       " << FormatTime(avg_ns) << "\n";
    std::cout << "  Throughput:        " << FormatThroughput(throughput) << "\n\n";

    e2e_total_s = elapsed;

    logger.ClearSinks();
    logger.FlushAll();
}

// ---------------------------------------------------------------------------
// 场景 3：多线程吞吐（4/8/16/32 线程）
// ---------------------------------------------------------------------------
void BenchMultiThreadThroughput(int max_threads, int per_thread) {
    PrintDivider("Scenario 3: Multi-Thread Throughput (Scalability)");
    std::cout << Color::DIM
              << "  Measuring concurrent write throughput.\n"
              << "  More threads = more lock contention on the queue.\n"
              << "  Compare across thread counts to see scalability.\n\n"
              << Color::RESET;

    auto& logger = LoggerCore::GetInstance();
    logger.ClearSinks();
    logger.AddSink(std::make_unique<CountingSink>());

    std::vector<int> thread_counts = {4, 8, 16, 32};

    // ---- 场景 3 表格：带网格线的并发吞吐表 ----
    std::cout << "\n";
    const int kColThreads   = 10;
    const int kColTotal     = 12;
    const int kColTime      = 12;
    const int kColThrough   = 18;
    const int kColAvg       = 12;
    const int kRowW = kColThreads + kColTotal + kColTime + kColThrough + kColAvg + 8;

    auto hline = [&](const std::string& l, const std::string& m,
                     const std::string& r) {
        std::cout << l << RepeatStr(kTableHL, kColThreads)
                  << m << RepeatStr(kTableHL, kColTotal)
                  << m << RepeatStr(kTableHL, kColTime)
                  << m << RepeatStr(kTableHL, kColThrough)
                  << m << RepeatStr(kTableHL, kColAvg)
                  << r << "\n";
    };
    auto cell = [](const std::string& s, int w, bool right) {
        if (right) return std::string(w - s.size(), ' ') + s;
        return s + std::string(w - s.size(), ' ');
    };

    std::cout << Color::CYAN << "┌" << RepeatStr(kTableHL, kRowW - 2) << "┐\n"
              << Color::RESET;
    hline("├", "┼", "┤");

    // 表头
    std::cout << Color::BOLD << "│"
              << cell("Threads",   kColThreads, false) << "│"
              << cell("Total Logs", kColTotal,   true)  << "│"
              << cell("Time",      kColTime,     true)  << "│"
              << cell("Throughput", kColThrough, true)  << "│"
              << cell("Avg/Log",   kColAvg,      true)  << "│"
              << Color::RESET << "\n";

    hline("├", "┼", "┤");

    for (int n : thread_counts) {
        if (n > max_threads) break;

        logger.FlushAll();

        LogLevel runtime_level = logger.GetRuntimeLogLevel();

        auto start = Clock::now();
        std::vector<std::thread> threads;
        threads.reserve(n);
        for (int t = 0; t < n; ++t) {
            threads.emplace_back([t, per_thread, runtime_level]() {
                for (int i = 0; i < per_thread; ++i) {
                    SPDIAG_LOG(runtime_level, LogModule::RUNTIME,
                                    "bench t=%d i=%d", t, i);
                }
            });
        }
        for (auto& th : threads) th.join();
        logger.FlushAll();
        auto end = Clock::now();

        double elapsed = std::chrono::duration<double>(end - start).count();
        size_t total = static_cast<size_t>(n) * per_thread;
        double throughput = (elapsed > 0) ? total / elapsed : 0;
        double avg_ns = (total > 0) ? (elapsed * 1e9) / total : 0;

        std::string row_threads = "T=" + std::to_string(n);
        std::string row_total   = std::to_string(total);
        char time_buf[32];
        std::snprintf(time_buf, sizeof(time_buf), "%.4fs", elapsed);
        std::string row_time   = time_buf;
        std::string row_through = FormatThroughput(throughput);
        std::string row_avg    = FormatTime(avg_ns);

        std::cout << Color::CYAN
                  << "│" << cell(row_threads, kColThreads, false)
                  << "│" << cell(row_total,   kColTotal,    true)
                  << "│" << cell(row_time,    kColTime,     true)
                  << "│" << cell(row_through, kColThrough,  true)
                  << "│" << cell(row_avg,     kColAvg,      true)
                  << "│" << Color::RESET << "\n";
    }

  hline("├", "┼", "┤");
    std::cout << Color::CYAN << "└" << RepeatStr(kTableHL, kRowW - 2) << "┘\n"
              << Color::RESET;

    std::cout << "  Where: TotalLogs = threads × perThread\n"
              << "         Time = total wall-clock (incl FlushAll)\n"
              << "         Throughput = TotalLogs / Time\n"
              << "         Avg/Log = Time / TotalLogs\n\n";

    logger.ClearSinks();
    logger.FlushAll();
}

// ---------------------------------------------------------------------------
// 场景 4：高压丢弃（慢盘模拟）
// ---------------------------------------------------------------------------
void BenchHighPressureDrop(int slow_ms) {
    PrintDivider("Scenario 4: High-Pressure Drop Rate");
    std::cout << Color::DIM
              << "  Simulating slow disk (" << slow_ms
              << "ms per Write) to measure drop rate.\n"
              << "  Drop rate = % of messages lost when queue overflows.\n\n"
              << Color::RESET;

    auto& logger = LoggerCore::GetInstance();
    logger.ClearSinks();

    auto slow = std::make_unique<SlowSink>(
        std::chrono::milliseconds(slow_ms));
    auto* slow_raw = slow.get();
    logger.AddSink(std::move(slow));

    const int num_threads = 8;
    const int per_thread = 10000;
    size_t emitted = static_cast<size_t>(num_threads) * per_thread;

    LogLevel runtime_level = logger.GetRuntimeLogLevel();

    auto start = Clock::now();
    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([t, per_thread, runtime_level]() {
            for (int i = 0; i < per_thread; ++i) {
                SPDIAG_LOG(runtime_level, LogModule::RUNTIME,
                                "stress t=%d i=%d padding-padding-padding",
                                t, i);
            }
        });
    }
    for (auto& th : threads) th.join();
    logger.FlushAll();
    auto end = Clock::now();

    double elapsed = std::chrono::duration<double>(end - start).count();
    size_t written = slow_raw->Written();
    size_t dropped = logger.GetDroppedCount();
    double drop_rate = (emitted > 0) ? 100.0 * dropped / emitted : 0.0;

    // ---- 场景 4 表格：丢弃率指标表 ----
    const int kLabelW = 22;
    const int kValueW = 22;
    auto cell2 = [](const std::string& s, int w, bool right) {
        if (right) return std::string(w - s.size(), ' ') + s;
        return s + std::string(w - s.size(), ' ');
    };
    auto grid2 = [&](const std::string& l, const std::string& m,
                     const std::string& r) {
        std::cout << l << RepeatStr(kTableHL, kLabelW)
                  << m << RepeatStr(kTableHL, kValueW) << r << "\n";
    };

    std::cout << "\n";
    grid2("┌", "┬", "┐");
    std::cout << Color::BOLD
              << "│" << cell2("Metric", kLabelW, false)
              << "│" << cell2("Value",  kValueW, true)
              << "│" << Color::RESET << "\n";
    grid2("├", "┼", "┤");
    std::cout << "│" << cell2("Emitted (total)",      kLabelW, false)
              << "│" << cell2(std::to_string(emitted),  kValueW, true)
              << "│" << "\n";
    std::cout << "│" << cell2("Written (by sink)",    kLabelW, false)
              << "│" << cell2(std::to_string(written),  kValueW, true)
              << "│" << "\n";
    std::cout << "│" << cell2("Dropped (queue full)", kLabelW, false)
              << "│" << cell2(std::to_string(dropped),  kValueW, true)
              << "│" << "\n";

    char drop_buf[32];
    std::snprintf(drop_buf, sizeof(drop_buf), "%.2f%%", drop_rate);
    std::cout << Color::RED
              << "│" << cell2("Drop Rate", kLabelW, false)
              << "│" << cell2(drop_buf,   kValueW, true)
              << "│" << Color::RESET << "\n";

    char elapsed_buf[32];
    std::snprintf(elapsed_buf, sizeof(elapsed_buf), "%.4f s", elapsed);
    std::cout << "│" << cell2("Elapsed",    kLabelW, false)
              << "│" << cell2(elapsed_buf,  kValueW, true)
              << "│" << "\n";
    std::cout << "│" << cell2("Throughput",  kLabelW, false)
              << "│" << cell2(FormatThroughput(
                     elapsed > 0 ? emitted / elapsed : 0), kValueW, true)
              << "│" << "\n";

    grid2("└", "┴", "┘");

    if (SPDIAG_ASYNC_ENQUEUE_TIMEOUT_MS == 0) {
        std::cout << "\n  " << Color::YELLOW
                  << "[NOTE] TIMEOUT=0 (non-blocking mode) — drops are"
                  " expected under sustained pressure.\n"
                  << Color::RESET;
    } else {
        std::cout << "\n  " << Color::YELLOW
                  << "[NOTE] TIMEOUT=" << SPDIAG_ASYNC_ENQUEUE_TIMEOUT_MS
                  << "ms — business threads may be blocked up to this duration"
                  << " per enqueue when queue is full.\n"
                  << Color::RESET;
    }

    logger.ClearSinks();
    logger.FlushAll();
}

// ---------------------------------------------------------------------------
// 场景 5：分位延迟直方图
// ---------------------------------------------------------------------------
void BenchLatencyHistogram(int iterations) {
    PrintDivider("Scenario 5: Latency Histogram & Percentiles");
    std::cout << Color::DIM
              << "  Collecting per-enqueue latency samples.\n"
              << "  Shows P50/P99/P999 distribution and bucket histogram.\n"
              << "  Batched into 10K chunks to avoid queue overflow.\n\n"
              << Color::RESET;

    auto& logger = LoggerCore::GetInstance();
    logger.ClearSinks();
    logger.AddSink(std::make_unique<CountingSink>());

    LogLevel runtime_level = logger.GetRuntimeLogLevel();

    // Warm-up
    for (int i = 0; i < 1000; ++i) {
        SPDIAG_LOG(runtime_level, LogModule::SDK, "warmup %d", i);
    }
    logger.FlushAll();

    const int sample_size = std::min(iterations, 500000);
    std::vector<double> latencies_ns;
    latencies_ns.reserve(sample_size);

    constexpr int kBatchSize = 10000;
    for (int collected = 0; collected < sample_size; ) {
        int batch = std::min(kBatchSize, sample_size - collected);
        for (int i = 0; i < batch; ++i) {
            auto t0 = Clock::now();
            SPDIAG_LOG(runtime_level, LogModule::SDK,
                            "latency sample %d", collected + i);
            auto t1 = Clock::now();
            latencies_ns.push_back(
                std::chrono::duration<double, std::nano>(t1 - t0).count());
        }
        logger.FlushAll();
        collected += batch;
    }

    std::sort(latencies_ns.begin(), latencies_ns.end());
    size_t n = latencies_ns.size();

    auto percentile = [&](double p) -> double {
        size_t idx = static_cast<size_t>(p * n / 100.0);
        if (idx >= n) idx = n - 1;
        return latencies_ns[idx];
    };

    double total_ns = 0;
    for (double v : latencies_ns) total_ns += v;
    double avg_ns = total_ns / n;

    std::cout << "  Sample size: " << n << " operations\n\n";

    // ---- 场景 5 分位延迟表：带网格线 ----
    const int kPColName  = 18;
    const int kPColLat   = 20;
    const int kPColCum   = 16;
    auto cellp = [](const std::string& s, int w, bool right) {
        if (right) return std::string(w - s.size(), ' ') + s;
        return s + std::string(w - s.size(), ' ');
    };
    auto gridp = [&](const std::string& l, const std::string& m,
                     const std::string& r) {
        std::cout << l << RepeatStr(kTableHL, kPColName)
                  << m << RepeatStr(kTableHL, kPColLat)
                  << m << RepeatStr(kTableHL, kPColCum) << r << "\n";
    };

    std::cout << "\n";
    gridp("┌", "┬", "┐");
    std::cout << Color::BOLD
              << "│" << cellp("Percentile",      kPColName, false)
              << "│" << cellp("Latency",        kPColLat,  true)
              << "│" << cellp("Cumulative %",  kPColCum,  true)
              << "│" << Color::RESET << "\n";
    gridp("├", "┼", "┤");

    struct PercentileRow { double p; const char* label; };
    PercentileRow rows[] = {
        {50.0,   "P50 (median)"},
        {90.0,   "P90"},
        {99.0,   "P99"},
        {99.9,   "P99.9"},
        {99.99,  "P99.99"},
        {100.0,  "Max"},
    };
    for (auto& r : rows) {
        double lat = (r.p == 100.0)
            ? latencies_ns.back()
            : percentile(r.p);
        char pct_buf[32];
        std::snprintf(pct_buf, sizeof(pct_buf), "%.3f%%+", r.p);
        std::cout << "│" << cellp(r.label,            kPColName, false)
                  << "│" << cellp(FormatTime(lat),   kPColLat,  true)
                  << "│" << cellp(pct_buf,           kPColCum,  true)
                  << "│" << "\n";
    }
    gridp("└", "┴", "┘");

    std::cout << "\n  Average:  " << FormatTime(avg_ns) << "\n\n";

    // 分布直方图
    struct Bucket { double lower; double upper; size_t count; };
    std::vector<Bucket> buckets = {
        {   0,     100, 0},
        { 100,     200, 0},
        { 200,     500, 0},
        { 500,    1000, 0},
        {1000,    5000, 0},
        {5000,   10000, 0},
        {10000,  50000, 0},
        {50000, 1e12,  0},
    };

    for (double v : latencies_ns) {
        for (auto& b : buckets) {
            if (v >= b.lower && v < b.upper) { ++b.count; break; }
        }
    }

    std::cout << "  Latency Distribution:\n";
    for (auto& b : buckets) {
        if (b.count == 0) continue;
        double pct = 100.0 * b.count / n;
        // 用 # 字符画简易直方图
        int bar_len = static_cast<int>(pct / 2.0);
        if (bar_len > 40) bar_len = 40;
        std::string bar(bar_len, '#');
        std::cout << "    " << std::left << std::setw(18)
                  << ("[" + std::to_string((int)b.lower) + "-" +
                      std::to_string((int)b.upper) + "ns]")
                  << std::right << std::setw(7) << b.count
                  << " (" << std::fixed << std::setprecision(1)
                  << std::setw(5) << pct << "%) "
                  << Color::GREEN << bar << Color::RESET << "\n";
    }

    logger.ClearSinks();
    logger.FlushAll();
}

// ---------------------------------------------------------------------------
// 场景 6：运行时日志级别过滤开销
// 评估"运行时传参日志级别"相比"编译时过滤（假设开销为0）"的额外开销
// ---------------------------------------------------------------------------

// NoOpSink：丢弃所有写入，用于消除 I/O 开销只测量过滤+格式化+入队开销
class NoOpSink : public LogSink {
public:
    void Write(const std::string&) override {}
    void Flush() override {}
};

void BenchRuntimeFilterOverhead(int iterations, LogLevel runtime_level) {
    PrintDivider("Scenario 6: Runtime Filter Overhead");
    std::cout << Color::DIM
              << "  评估运行时日志级别过滤的开销。\n"
              << "  假设编译时过滤开销为 0，运行时过滤的额外开销 = 被过滤日志的单条耗时。\n"
              << "  Runtime level: " << GetLogLevelName(runtime_level) << "\n\n"
              << Color::RESET;

    auto& logger = LoggerCore::GetInstance();
    logger.ClearSinks();
    logger.AddSink(std::make_unique<NoOpSink>());

    // 1. 测量"被过滤"路径开销：写入级别 < runtime_level（仅 atomic load + branch + return）
    LogLevel filtered_level = LogLevel::DEBUG;
    if (runtime_level == LogLevel::DEBUG) {
        // runtime=DEBUG 时所有日志都通过，没有"被过滤"路径
        std::cout << Color::YELLOW
                  << "  [SKIP] Runtime level is DEBUG — no log can be filtered.\n\n"
                  << Color::RESET;
        logger.ClearSinks();
        return;
    }

    logger.SetRuntimeLogLevel(runtime_level);
    logger.FlushAll();

    auto t1 = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        SPDIAG_LOG(filtered_level, LogModule::SDK, "filtered msg %d", i);
    }
    auto t2 = Clock::now();
    double filtered_ns = std::chrono::duration<double, std::nano>(t2 - t1).count() / iterations;

    // 2. 测量"通过过滤"路径开销：写入级别 = runtime_level（格式化 + 入队 + NoOpSink写入）
    auto t3 = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        SPDIAG_LOG(runtime_level, LogModule::SDK, "passed msg %d", i);
    }
    logger.FlushAll();
    auto t4 = Clock::now();
    double passed_ns = std::chrono::duration<double, std::nano>(t4 - t3).count() / iterations;

    // 3. 测量多线程"被过滤"路径吞吐
    const int num_threads = 8;
    int per_thread = iterations / num_threads;
    logger.SetRuntimeLogLevel(runtime_level);

    auto mt_start = Clock::now();
    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([t, per_thread, filtered_level]() {
            for (int i = 0; i < per_thread; ++i) {
                SPDIAG_LOG(filtered_level, LogModule::RUNTIME,
                           "mt filtered t=%d i=%d", t, i);
            }
        });
    }
    for (auto& th : threads) th.join();
    auto mt_end = Clock::now();
    double mt_wall_ns = std::chrono::duration<double, std::nano>(mt_end - mt_start).count();
    size_t mt_total = static_cast<size_t>(num_threads) * per_thread;
    double mt_throughput = (mt_wall_ns > 0) ? mt_total / (mt_wall_ns / 1e9) : 0;

    // 输出表格
    const int kColName = 36;
    const int kColVal  = 20;
    auto cellr = [](const std::string& s, int w, bool right) {
        if (right) return std::string(w - s.size(), ' ') + s;
        return s + std::string(w - s.size(), ' ');
    };
    auto gridr = [&](const std::string& l, const std::string& m,
                     const std::string& r) {
        std::cout << l << RepeatStr(kTableHL, kColName)
                  << m << RepeatStr(kTableHL, kColVal) << r << "\n";
    };

    std::cout << "\n";
    gridr("┌", "┬", "┐");
    std::cout << Color::BOLD
              << "│" << cellr("Metric", kColName, false)
              << "│" << cellr("Value",  kColVal,  true)
              << "│" << Color::RESET << "\n";
    gridr("├", "┼", "┤");

    std::cout << "│" << cellr("Filtered path (per call)",  kColName, false)
              << "│" << cellr(FormatTime(filtered_ns),     kColVal,  true) << "│\n";
    std::cout << "│" << cellr("Passed path (per call)",    kColName, false)
              << "│" << cellr(FormatTime(passed_ns),       kColVal,  true) << "│\n";

    char overhead_buf[64];
    std::snprintf(overhead_buf, sizeof(overhead_buf), "%s",
                  FormatTime(filtered_ns).c_str());
    std::cout << Color::YELLOW
              << "│" << cellr("Runtime filter overhead",   kColName, false)
              << "│" << cellr(overhead_buf,                kColVal,  true)
              << "│" << Color::RESET << "\n";

    char mt_buf[64];
    std::snprintf(mt_buf, sizeof(mt_buf), "%.2f M logs/s", mt_throughput / 1e6);
    std::cout << "│" << cellr("Multi-thread filtered (8T)", kColName, false)
              << "│" << cellr(mt_buf,                       kColVal,  true) << "│\n";

    gridr("└", "┴", "┘");

    std::cout << "\n  " << Color::DIM
              << "Interpretation:\n"
              << "    - Filtered path: atomic relaxed load + branch + return ≈ 3-5 ns\n"
              << "    - Passed path:   format + enqueue + NoOpSink write ≈ 1.5-2 μs\n"
              << "    - Overhead:      runtime filter cost vs compile-time (assumed 0)\n"
              << "    - MT throughput: multi-thread filtered throughput (T=8)\n"
              << Color::RESET << "\n";

    logger.ClearSinks();
}

// ---------------------------------------------------------------------------
// 最终汇总
// ---------------------------------------------------------------------------
void PrintSummary(double baseline_ns, double enqueue_avg_ns,
                  double e2e_total_s, int iterations) {
    std::cout << "\n" << Color::BOLD << Color::CYAN
              << "╔══════════════════════════════════════════════════════════════╗\n"
              << "║                      Summary & Conclusion                   ║\n"
              << "╚══════════════════════════════════════════════════════════════╝\n"
              << Color::RESET << "\n";

    double overhead_ns = enqueue_avg_ns - baseline_ns;
    double overhead_pct = (enqueue_avg_ns > 0)
        ? 100.0 * overhead_ns / enqueue_avg_ns : 0.0;
    double e2e_avg_ns = (iterations > 0) ? (e2e_total_s * 1e9) / iterations : 0;

    // ---- Summary 表格：带网格线 ----
    const int kSColName  = 34;
    const int kSColVal   = 28;
    auto cells = [](const std::string& s, int w, bool right) {
        if (right) return std::string(w - s.size(), ' ') + s;
        return s + std::string(w - s.size(), ' ');
    };
    auto grids = [&](const std::string& l, const std::string& m,
                     const std::string& r) {
        std::cout << l << RepeatStr(kTableHL, kSColName)
                  << m << RepeatStr(kTableHL, kSColVal) << r << "\n";
    };

    char overhead_buf[64];
    std::snprintf(overhead_buf, sizeof(overhead_buf),
                  "%s (+%.1f%%)",
                  FormatTime(overhead_ns).c_str(), overhead_pct);

    char e2e_through_buf[64];
    std::snprintf(e2e_through_buf, sizeof(e2e_through_buf), "%s",
                  FormatThroughput(
                      e2e_total_s > 0 ? iterations / e2e_total_s : 0).c_str());

    std::cout << "\n";
    grids("┌", "┬", "┐");
    std::cout << Color::BOLD
              << "│" << cells("Metric",               kSColName, false)
              << "│" << cells("Value",                kSColVal,  true)
              << "│" << Color::RESET << "\n";
    grids("├", "┼", "┤");

    std::cout << "│" << cells("NoOp Baseline (per call)",
                 kSColName, false)
              << "│" << cells(FormatTime(baseline_ns),
                 kSColVal, true) << "│" << "\n";

    std::cout << Color::GREEN
              << "│" << cells("Async Enqueue (per log)",
                 kSColName, false)
              << "│" << cells(FormatTime(enqueue_avg_ns),
                 kSColVal, true)
              << "│" << Color::RESET << "\n";

    std::cout << Color::YELLOW
              << "│" << cells("  Net logging overhead",
                 kSColName, false)
              << "│" << cells(overhead_buf,
                 kSColVal, true)
              << "│" << Color::RESET << "\n";

    std::cout << "│" << cells("End-to-End (per log, w/ flush)",
                 kSColName, false)
              << "│" << cells(FormatTime(e2e_avg_ns),
                 kSColVal, true) << "│" << "\n";

    std::cout << "│" << cells("End-to-End throughput",
                 kSColName, false)
              << "│" << cells(e2e_through_buf,
                 kSColVal, true) << "│" << "\n";

    grids("└", "┴", "┘");

    std::cout << "\n  " << Color::DIM
              << "Interpretation:\n"
              << "    - Baseline: overhead of calling an empty function\n"
              << "    - Enqueue:  time to format + enqueue one log (async, returns immediately)\n"
              << "    - Net:      pure logging cost = Enqueue - Baseline\n"
              << "    - E2E:      full pipeline including background write + FlushAll\n"
              << Color::RESET << "\n";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    BenchConfig cfg;
    LogLevel runtime_level;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            cfg.iterations = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            cfg.max_threads = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            cfg.slow_ms = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-h") == 0 ||
                   std::strcmp(argv[i], "--help") == 0) {
            std::cout
                << "Usage: benchmark_async_logger [options]\n\n"
                << "Options:\n"
                << "  -n <N>       Iterations per benchmark (default 100000)\n"
                << "  -t <N>       Max threads for throughput test (default 32)\n"
                << "  -s <N>       SlowSink delay in ms (default 2)\n";
            PrintLogLevelHelp();
            std::cout
                << "  -h           Show this help message\n\n"
                << "Note: Benchmark logs use the runtime level to measure\n"
                << "      the cost of logs at the configured level.\n\n"
                << "Examples:\n"
                << "  benchmark_async_logger\n"
                << "  benchmark_async_logger --log-level DEBUG\n"
                << "  benchmark_async_logger -n 500000 --log-level=INFO\n"
                << "  benchmark_async_logger -n 200000 -t 16 -s 5\n";
            return 0;
        }
    }

    // 解析并设置运行时日志级别
    // 默认 ERROR，可通过 --log-level 覆盖；场景 1-5 使用该级别测量全路径开销，
    // 场景 6 评估运行时过滤相对编译时过滤（假设开销为0）的额外开销。
    ParseLogLevelArg(argc, argv, LogLevel::ERROR, runtime_level);
    LoggerCore::GetInstance().SetRuntimeLogLevel(runtime_level);

    PrintHeader(cfg);

    // 场景 0：NoOp 基线
    double baseline_ns = 0;
    BenchNoOpBaseline(cfg.iterations, baseline_ns);

    // 场景 1：单线程 Enqueue 纯延迟
    double enqueue_avg_ns = 0;
    BenchSingleThreadEnqueue(cfg.iterations, baseline_ns, enqueue_avg_ns);

    // 场景 2：单线程端到端延迟
    double e2e_total_s = 0;
    BenchSingleThreadEndToEnd(cfg.iterations, e2e_total_s);

    // 场景 3：多线程吞吐
    const int per_thread = cfg.iterations;
    BenchMultiThreadThroughput(cfg.max_threads, per_thread);

    // 场景 4：高压丢弃
    BenchHighPressureDrop(cfg.slow_ms);

    // 场景 5：分位延迟直方图
    BenchLatencyHistogram(cfg.iterations);

    // 场景 6：运行时日志级别过滤开销
    BenchRuntimeFilterOverhead(cfg.iterations, runtime_level);

    // 最终汇总
    PrintSummary(baseline_ns, enqueue_avg_ns, e2e_total_s, cfg.iterations);

    std::cout << "\n" << Color::BOLD << Color::GREEN
              << "  Benchmark complete." << Color::RESET << "\n";

    return 0;
}