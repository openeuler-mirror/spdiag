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

// 异步日志测试：覆盖 docs/ASYNC_LOG_DESIGN.md 第 9 节全部场景。
//
// 测试分两类：
//   1. AsyncLogWorker 直接测试（验证队列/计数/刷盘语义）
//   2. LoggerCore 异步集成测试（端到端验证）

#include <gtest/gtest.h>

#include "spdiag/logger.h"
#include "spdiag/log_macros.h"
#include "spdiag/log_config.h"
#include "logger/async_worker.h"
#include "logger/sinks.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

namespace {

// 收集所有写入消息的 sink，用于断言异步输出结果。
class CollectingSink : public SpDiag::LogSink {
public:
    void Write(const std::string& message) override {
        std::lock_guard<std::mutex> lock(mutex_);
        messages_.push_back(message);
    }

    void Flush() override {}

    std::vector<std::string> GetMessages() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return messages_;
    }

    size_t Count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return messages_.size();
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        messages_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::vector<std::string> messages_;
};

// 等待条件成立或超时。避免依赖 wall-clock 的 sleep_for，提高测试稳定性。
bool WaitFor(std::function<bool()> pred,
             std::chrono::milliseconds timeout = std::chrono::seconds(2),
             std::chrono::milliseconds step = std::chrono::milliseconds(5)) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(step);
    }
    return pred();
}

// 构造一个返回单一 sink 快照的 SinkGetter，供 AsyncLogWorker 直接测试使用。
SpDiag::AsyncLogWorker::SinkGetter MakeSinkGetter(std::shared_ptr<CollectingSink> sink) {
    return [sink]() -> SpDiag::AsyncLogWorker::SinkList {
        return {sink};
    };
}

// 捕获 std::cerr 输出的 RAII 辅助类，用于验证丢弃告警（首次告警 + 区间汇总）
// 输出到 stderr。注意：仅适用于串行读取场景（读取前先 StopAndFlush 保证
// worker 线程已退出，不再并发写 std::cerr）。
class StderrCapture {
public:
    StderrCapture() : old_buf_(std::cerr.rdbuf()) {
        std::cerr.rdbuf(oss_.rdbuf());
    }
    ~StderrCapture() {
        std::cerr.rdbuf(old_buf_);
    }
    std::string Get() const { return oss_.str(); }

private:
    std::ostringstream oss_;
    std::streambuf* old_buf_;
};

// LoggerCore 单例测试夹具：确保每个测试用例的 sink 隔离。
// SetUp 清空前序测试残留的 sinks，TearDown 排空队列。
class LoggerCoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        SpDiag::LoggerCore::GetInstance().ClearSinks();
        // 设置运行时级别为 DEBUG，确保所有级别日志均通过过滤
        SpDiag::LoggerCore::GetInstance().SetRuntimeLogLevel(
            SpDiag::LogLevel::DEBUG);
    }
    void TearDown() override {
        SpDiag::LoggerCore::GetInstance().FlushAll();
        // 恢复默认运行时级别
        SpDiag::LoggerCore::GetInstance().SetRuntimeLogLevel(
            SpDiag::LogLevel::ERROR);
    }
};

} // anonymous namespace

// ============================================================================
// 9.1 队列满丢弃行为
// ============================================================================

TEST(AsyncLogWorkerQueueFullTest, EnqueueFailsWhenQueueFull) {
    // 容量设为 2，不启动后台线程，Enqueue 第 3 条应等待 1ms 后返回 false
    auto sink = std::make_shared<CollectingSink>();
    SpDiag::AsyncLogWorker worker(2, MakeSinkGetter(sink));
    // 注意：不调用 Start()，队列不会被消费

    EXPECT_TRUE(worker.Enqueue("msg1"));
    EXPECT_TRUE(worker.Enqueue("msg2"));

    auto start = std::chrono::steady_clock::now();
    bool result = worker.Enqueue("msg3");
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(result);
    // 应至少等待了 kEnqueueTimeout（1ms），留出调度抖动空间
    EXPECT_GE(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 1);
}

TEST(AsyncLogWorkerQueueFullTest, DroppedCountIncrements) {
    auto sink = std::make_shared<CollectingSink>();
    SpDiag::AsyncLogWorker worker(2, MakeSinkGetter(sink));
    // 不启动后台线程，连续 Enqueue 超容量条

    EXPECT_TRUE(worker.Enqueue("msg1"));
    EXPECT_TRUE(worker.Enqueue("msg2"));

    size_t dropped_before = worker.DroppedCount();
    for (int i = 0; i < 3; ++i) {
        worker.Enqueue("overflow-" + std::to_string(i));
    }
    size_t dropped_after = worker.DroppedCount();

    EXPECT_EQ(dropped_after - dropped_before, 3u);
}

TEST(AsyncLogWorkerQueueFullTest, DropAlertFirstImmediateThenIntervalSummary) {
    // 新丢弃告警方案（LOG_MODULE_OPTIMIZATION_PLAN.md 方案 A）：
    //   1. 首次丢弃立即向 stderr 告警一次（first_drop_warned_ 0→1 只触发一次），
    //      不再像旧方案那样经 sink 批量输出（≥100 条才告警）。
    //   2. 此后每 summary_interval_ 输出一条区间速率汇总（本区间丢弃数/累计数/速率）。
    //   3. 无丢弃的区间自动静默（不刷屏）。
    auto sink = std::make_shared<CollectingSink>();
    // 注入 20ms 汇总间隔，便于在测试时间尺度内验证区间汇总。
    SpDiag::AsyncLogWorker worker(1, MakeSinkGetter(sink),
                                  std::chrono::milliseconds(20));
    // 不启动后台线程，先填满队列，后续 Enqueue 全部丢弃
    EXPECT_TRUE(worker.Enqueue("only-kept-msg"));

    StderrCapture capture;

    // 连续丢弃若干条：每次 Enqueue 等待 1ms 后超时丢弃
    for (int i = 0; i < 3; ++i) {
        EXPECT_FALSE(worker.Enqueue("overflow-" + std::to_string(i)));
    }
    EXPECT_GE(worker.DroppedCount(), 3u);

    // 首次丢弃的告警应立即出现在 stderr（无需后台线程参与）
    EXPECT_NE(capture.Get().find("first log message dropped"), std::string::npos)
        << "首次丢弃应立即向 stderr 告警";

    // 启动后台线程，等待一个汇总区间过去后唤醒 worker 输出区间汇总
    worker.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(40));  // 2× 间隔
    worker.FlushSync();  // 唤醒 worker 执行区间汇总检查
    worker.StopAndFlush();

    const std::string captured = capture.Get();
    EXPECT_NE(captured.find("log messages dropped in last"), std::string::npos)
        << "区间速率汇总应输出到 stderr";
    EXPECT_NE(captured.find("total 3"), std::string::npos)
        << "区间汇总应包含累计丢弃数（3 条）";
}

TEST(AsyncLogWorkerQueueFullTest, DropAlertSilentWhenNoDrops) {
    // 无丢弃时不应输出任何告警（区间自动静默，避免刷屏）。
    auto sink = std::make_shared<CollectingSink>();
    SpDiag::AsyncLogWorker worker(8, MakeSinkGetter(sink),
                                  std::chrono::milliseconds(20));
    StderrCapture capture;

    worker.Start();
    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(worker.Enqueue("msg-" + std::to_string(i)));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(40));  // 2× 间隔
    worker.FlushSync();
    worker.StopAndFlush();

    EXPECT_EQ(worker.DroppedCount(), 0u);
    EXPECT_EQ(capture.Get().find("first log message dropped"), std::string::npos)
        << "无丢弃时不应输出首次告警";
    EXPECT_EQ(capture.Get().find("log messages dropped in last"), std::string::npos)
        << "无丢弃时不应输出区间汇总";
}

// ============================================================================
// 9.2 计数器正确性
// ============================================================================

TEST(AsyncLogWorkerCounterTest, EnqueuedCountMatches) {
    auto sink = std::make_shared<CollectingSink>();
    SpDiag::AsyncLogWorker worker(10, MakeSinkGetter(sink));
    worker.Start();

    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(worker.Enqueue("msg-" + std::to_string(i)));
    }
    worker.FlushSync();

    EXPECT_EQ(worker.EnqueuedCount(), 5u);
}

TEST(AsyncLogWorkerCounterTest, ConservationEquation) {
    // EnqueuedCount + DroppedCount == TotalEmitted
    auto sink = std::make_shared<CollectingSink>();
    SpDiag::AsyncLogWorker worker(2, MakeSinkGetter(sink));
    // 不启动后台线程，制造丢弃

    const size_t total = 10;
    for (size_t i = 0; i < total; ++i) {
        worker.Enqueue("msg-" + std::to_string(i));
    }

    EXPECT_EQ(worker.EnqueuedCount() + worker.DroppedCount(), total);

    // 启动后台线程处理，CollectingSink 收到的条数应等于 EnqueuedCount
    // （丢弃告警走 stderr，不经 sink，因此 sink 只收日志消息本身）
    worker.Start();
    worker.FlushSync();
    EXPECT_EQ(sink->Count(), worker.EnqueuedCount());
}

TEST(AsyncLogWorkerCounterTest, QueueSizeAfterDrain) {
    auto sink = std::make_shared<CollectingSink>();
    SpDiag::AsyncLogWorker worker(100, MakeSinkGetter(sink));
    worker.Start();

    for (int i = 0; i < 50; ++i) {
        worker.Enqueue("msg-" + std::to_string(i));
    }
    worker.FlushSync();

    EXPECT_EQ(worker.QueueSize(), 0u);
}

TEST(AsyncLogWorkerCounterTest, QueueSizeDuringFill) {
    auto sink = std::make_shared<CollectingSink>();
    SpDiag::AsyncLogWorker worker(100, MakeSinkGetter(sink));
    // 不启动后台线程，Enqueue K 条，QueueSize 应等于 K

    const size_t k = 30;
    for (size_t i = 0; i < k; ++i) {
        worker.Enqueue("msg-" + std::to_string(i));
    }
    EXPECT_EQ(worker.QueueSize(), k);
}

// ============================================================================
// 9.3 Flush 排空后队列状况
// ============================================================================

TEST(AsyncLogWorkerFlushTest, FlushSyncDrainsQueueAndDelivers) {
    auto sink = std::make_shared<CollectingSink>();
    SpDiag::AsyncLogWorker worker(100, MakeSinkGetter(sink));
    worker.Start();

    for (int i = 0; i < 20; ++i) {
        worker.Enqueue("msg-" + std::to_string(i));
    }
    // FlushSync 返回时队列应已排空且所有消息已写入 sink
    worker.FlushSync();

    EXPECT_EQ(worker.QueueSize(), 0u);
    EXPECT_EQ(sink->Count(), 20u);
}

TEST(AsyncLogWorkerFlushTest, FlushSyncKeepsWorkerAlive) {
    // FlushSync 后仍可继续 Enqueue（与 StopAndFlush 的终态语义相对）
    auto sink = std::make_shared<CollectingSink>();
    SpDiag::AsyncLogWorker worker(100, MakeSinkGetter(sink));
    worker.Start();

    worker.Enqueue("first");
    worker.FlushSync();
    EXPECT_EQ(sink->Count(), 1u);

    EXPECT_TRUE(worker.Enqueue("second"));
    worker.FlushSync();
    EXPECT_EQ(sink->Count(), 2u);
}

// 验证 FlushSync 被唤醒时若队列非空，能重新触发 flush（重请求机制）
// 使用带延迟的 sink 确保消息处理期间主线程有时间追加新消息
class SlowCollectingSink : public CollectingSink {
public:
    void Write(const std::string& msg) override {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        CollectingSink::Write(msg);
    }
};

TEST(AsyncLogWorkerFlushTest, FlushSyncReRequestWhenQueueNotEmpty) {
    auto sink = std::make_shared<SlowCollectingSink>();
    SpDiag::AsyncLogWorker worker(1000, MakeSinkGetter(sink));
    worker.Start();

    // 先入队大量消息，利用慢 sink 使 worker 处理时间变长
    for (int i = 0; i < 200; ++i) {
        worker.Enqueue("batch1-" + std::to_string(i));
    }

    // 在另一线程调用 FlushSync
    std::thread flush_thread([&worker]() {
        worker.FlushSync();
    });

    // 慢 sink 处理 200 条约需 20ms，确保 FlushSync 在此期间处于等待状态
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // 追加新消息，验证 FlushSync 能正确处理"被唤醒时队列非空"的情况
    worker.Enqueue("batch2-msg1");
    worker.Enqueue("batch2-msg2");
    worker.Enqueue("batch2-msg3");

    flush_thread.join();

    // FlushSync 返回后，所有消息都应被写入 sink
    EXPECT_EQ(sink->Count(), 203u);
}

// 验证 FlushSync 不杀死后台线程：调用后仍可正常 Enqueue 和 Flush
TEST(AsyncLogWorkerFlushTest, FlushAllDoesNotKillWorker) {
    auto sink = std::make_shared<CollectingSink>();
    SpDiag::AsyncLogWorker worker(100, MakeSinkGetter(sink));
    worker.Start();

    // 第一次 FlushSync
    worker.Enqueue("before-flush");
    worker.FlushSync();
    ASSERT_EQ(sink->Count(), 1u);

    // FlushSync 后线程应继续存活，后续 Enqueue 仍成功
    EXPECT_TRUE(worker.Enqueue("after-flush-1"));
    EXPECT_TRUE(worker.Enqueue("after-flush-2"));
    worker.FlushSync();
    EXPECT_EQ(sink->Count(), 3u);
}

TEST(AsyncLogWorkerFlushTest, StopAndFlushProcessesRemaining) {
    auto sink = std::make_shared<CollectingSink>();
    SpDiag::AsyncLogWorker worker(100, MakeSinkGetter(sink));
    worker.Start();

    for (int i = 0; i < 15; ++i) {
        worker.Enqueue("msg-" + std::to_string(i));
    }
    // StopAndFlush 应处理完队列中剩余消息再退出
    worker.StopAndFlush();

    EXPECT_EQ(sink->Count(), 15u);
    EXPECT_EQ(worker.QueueSize(), 0u);
}

TEST(AsyncLogWorkerFlushTest, StopAndFlushMakesEnqueueReject) {
    auto sink = std::make_shared<CollectingSink>();
    SpDiag::AsyncLogWorker worker(100, MakeSinkGetter(sink));
    worker.Start();
    worker.StopAndFlush();

    // 停止后 Enqueue 应被拒绝
    EXPECT_FALSE(worker.Enqueue("after-stop"));
}

TEST(AsyncLogWorkerFlushTest, StartIsIdempotent) {
    auto sink = std::make_shared<CollectingSink>();
    SpDiag::AsyncLogWorker worker(100, MakeSinkGetter(sink));
    worker.Start();
    worker.Start();  // 重复 Start 应安全，不创建第二个线程
    worker.Enqueue("msg");
    worker.FlushSync();
    EXPECT_EQ(sink->Count(), 1u);
    worker.StopAndFlush();
}

TEST(AsyncLogWorkerFlushTest, DestructorFlushesPending) {
    auto sink = std::make_shared<CollectingSink>();
    {
        SpDiag::AsyncLogWorker worker(100, MakeSinkGetter(sink));
        worker.Start();
        for (int i = 0; i < 10; ++i) {
            worker.Enqueue("msg-" + std::to_string(i));
        }
        // 析构函数应调用 StopAndFlush，处理剩余消息
    }
    EXPECT_EQ(sink->Count(), 10u);
}

// ============================================================================
// 并发压力测试：验证多线程入队 + 后台批量出队的正确性
// ============================================================================

TEST(AsyncLogWorkerConcurrencyTest, MultiThreadEnqueue) {
    const int num_threads = 8;
    const int per_thread = 200;
    const int total = num_threads * per_thread;

    auto sink = std::make_shared<CollectingSink>();
    SpDiag::AsyncLogWorker worker(8192, MakeSinkGetter(sink));
    worker.Start();

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&worker, t, per_thread]() {
            for (int i = 0; i < per_thread; ++i) {
                worker.Enqueue("t" + std::to_string(t) + "-m" + std::to_string(i));
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }

    worker.FlushSync();

    // 队列容量 8192 >= total=1600，无丢弃
    EXPECT_EQ(worker.DroppedCount(), 0u);
    EXPECT_EQ(sink->Count(), static_cast<size_t>(total));
    EXPECT_EQ(worker.QueueSize(), 0u);
}

// ============================================================================
// LoggerCore 异步集成测试（使用 LoggerCoreTest 夹具实现测试隔离）
// ============================================================================

TEST_F(LoggerCoreTest, EndToEndDeliversToSink) {
    auto collector = std::make_unique<CollectingSink>();
    auto* raw = collector.get();
    SpDiag::LoggerCore::GetInstance().AddSink(std::move(collector));

    // 使用 ERROR 级别，确保日志始终输出（默认运行时级别为 ERROR）
    SPDIAG_LOG_ERROR(SpDiag::LogModule::SDK, "async end-to-end message %d", 42);
    SpDiag::LoggerCore::GetInstance().FlushAll();

    ASSERT_TRUE(WaitFor([raw] { return raw->Count() >= 1; }))
        << "Sink did not receive async message";
    auto msgs = raw->GetMessages();
    bool found = false;
    for (const auto& m : msgs) {
        if (m.find("async end-to-end message 42") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected async message not delivered to sink";
}

TEST_F(LoggerCoreTest, FlushAllDrainsQueue) {
    auto collector = std::make_unique<CollectingSink>();
    auto* raw = collector.get();
    SpDiag::LoggerCore::GetInstance().AddSink(std::move(collector));

    const int n = 100;
    for (int i = 0; i < n; ++i) {
        SPDIAG_LOG_ERROR(SpDiag::LogModule::SDK, "drain-test-%d", i);
    }
    // FlushAll 应阻塞到队列排空
    SpDiag::LoggerCore::GetInstance().FlushAll();

    size_t count = raw->Count();
    // 至少收到 n 条（其他测试可能也向此 sink 写入，但此 sink 是新注册的）
    EXPECT_GE(count, static_cast<size_t>(n));
}

TEST_F(LoggerCoreTest, MultiThreadStress) {
    auto collector = std::make_unique<CollectingSink>();
    auto* raw = collector.get();
    SpDiag::LoggerCore::GetInstance().AddSink(std::move(collector));

    const int num_threads = 8;
    const int per_thread = 100;
    const int total = num_threads * per_thread;

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([t, per_thread]() {
            for (int i = 0; i < per_thread; ++i) {
                SPDIAG_LOG_ERROR(SpDiag::LogModule::RUNTIME,
                                 "stress t%d i%d", t, i);
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }

    SpDiag::LoggerCore::GetInstance().FlushAll();

    EXPECT_GE(raw->Count(), static_cast<size_t>(total));
}

TEST_F(LoggerCoreTest, SignalWatcherFlushCallback) {
    // 验证 SignalWatcher 回调中调用 FlushAll 在异步模式下不死锁
    // （FlushSync 持 queue_mutex_ 等待，后台线程需能获取 queue_mutex_ 通知）
    auto collector = std::make_unique<CollectingSink>();
    auto* raw = collector.get();
    SpDiag::LoggerCore::GetInstance().AddSink(std::move(collector));

    for (int i = 0; i < 50; ++i) {
        SPDIAG_LOG_ERROR(SpDiag::LogModule::SDK, "pre-signal-%d", i);
    }

    // 模拟 SignalWatcher 回调：在另一线程调用 FlushAll，主线程同时继续写日志
    std::thread flush_thread([]() {
        SpDiag::LoggerCore::GetInstance().FlushAll();
    });

    // 同时继续入队
    for (int i = 0; i < 50; ++i) {
        SPDIAG_LOG_ERROR(SpDiag::LogModule::SDK, "during-signal-%d", i);
    }

    flush_thread.join();  // 不死锁即通过
    SpDiag::LoggerCore::GetInstance().FlushAll();

    EXPECT_GE(raw->Count(), static_cast<size_t>(100));
}

// ============================================================================
// 9.4 FileSink 滚动
// 注：FileSink 的 Write/Flush 仅由 AsyncLogWorker 后台单线程调用，无并发访问，
//     因此滚动逻辑本身不存在竞态。此处仅验证滚动行为正确性。
// ============================================================================

namespace {

// 生成唯一临时文件路径，避免多测试用例间相互干扰
std::string MakeTempLogPath(const char* tag) {
    std::string path = "/tmp/ubdiag_test_";
    path += tag;
    path += "_";
    path += std::to_string(::getpid());
    path += "_";
    path += std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    path += ".log";
    return path;
}

// 删除文件（忽略不存在）
void RemoveFile(const std::string& path) {
    std::remove(path.c_str());
}

// 统计文件是否存在
bool FileExists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

} // anonymous namespace

// 验证写入超过 max_size 后生成 .1 备份文件
TEST(AsyncLogWorkerFileSinkTest, RotationUnderAsync) {
    std::string path = MakeTempLogPath("rot");
    RemoveFile(path);
    RemoveFile(path + ".1");

    constexpr size_t kMaxSize = 256;  // 小容量便于触发滚动
    auto sink = std::make_shared<SpDiag::FileSink>(path, kMaxSize, 2);

    SpDiag::AsyncLogWorker::SinkList sinks{sink};
    SpDiag::AsyncLogWorker worker(1024, [&sinks]() { return sinks; });
    worker.Start();

    // 每条约 64 字节(含\n)，写 5 条约 320 字节 > 256 必定触发滚动
    const std::string prefix = "rotation-test-message-iteration-padding-padding-padding-padding-";
    for (int i = 0; i < 5; ++i) {
        worker.Enqueue(prefix + std::to_string(i));
    }
    worker.FlushSync();
    sink->Flush();

    EXPECT_TRUE(FileExists(path)) << "current log file should exist";
    EXPECT_TRUE(FileExists(path + ".1")) << "rotated backup .1 should exist";

    RemoveFile(path);
    RemoveFile(path + ".1");
}

// 验证 max_files 限制：写入超过 max_files * max_size 后，最旧备份被删除
TEST(AsyncLogWorkerFileSinkTest, MaxFilesLimit) {
    std::string path = MakeTempLogPath("maxf");
    // 清理可能的残留
    for (int i = 1; i <= 5; ++i) {
        RemoveFile(path + "." + std::to_string(i));
    }
    RemoveFile(path);

    constexpr size_t kMaxSize = 128;
    const int kMaxFiles = 2;
    auto sink = std::make_shared<SpDiag::FileSink>(path, kMaxSize, kMaxFiles);

    SpDiag::AsyncLogWorker::SinkList sinks{sink};
    SpDiag::AsyncLogWorker worker(1024, [&sinks]() { return sinks; });
    worker.Start();

    // 大量写入触发多次滚动，备份数应受限于 kMaxFiles
    for (int i = 0; i < 20; ++i) {
        worker.Enqueue("max-files-test-message-iteration-" + std::to_string(i));
    }
    worker.FlushSync();
    sink->Flush();

    // 验证：.1 应存在，.3 不应存在（受 kMaxFiles=2 限制）
    EXPECT_TRUE(FileExists(path + ".1")) << ".1 backup should exist";
    EXPECT_FALSE(FileExists(path + ".3")) << ".3 should be deleted (exceeds max_files)";

    // 清理
    for (int i = 1; i <= 5; ++i) {
        RemoveFile(path + "." + std::to_string(i));
    }
    RemoveFile(path);
}

// ============================================================================
// 9.5 多线程并发写（补充场景）
// ============================================================================

// 验证单线程内消息顺序保持（异步不破坏同线程顺序）
TEST_F(LoggerCoreTest, MessageOrderingPerThread) {
    auto collector = std::make_unique<CollectingSink>();
    auto* raw = collector.get();
    SpDiag::LoggerCore::GetInstance().AddSink(std::move(collector));

    const int num_messages = 200;
    for (int i = 0; i < num_messages; ++i) {
        SPDIAG_LOG_ERROR(SpDiag::LogModule::SDK, "order-%06d", i);
    }
    SpDiag::LoggerCore::GetInstance().FlushAll();

    auto msgs = raw->GetMessages();
    ASSERT_GE(msgs.size(), static_cast<size_t>(num_messages));

    // 单线程顺序应保持：每条消息的序号递增
    int last_seq = -1;
    for (const auto& m : msgs) {
        // 提取 "order-XXXXXX" 中的序号
        std::regex re("order-(\\d{6})");
        std::smatch match;
        ASSERT_TRUE(std::regex_search(m, match, re)) << "msg format unexpected: " << m;
        int seq = std::stoi(match[1].str());
        EXPECT_GT(seq, last_seq) << "顺序错乱: seq=" << seq << " last=" << last_seq;
        last_seq = seq;
    }
}

// 验证高并发写期间调用 AddSink 不崩溃，且新 sink 最终收到后续消息
TEST_F(LoggerCoreTest, AddSinkDuringLogging) {
    auto collector = std::make_unique<CollectingSink>();
    auto* raw = collector.get();
    SpDiag::LoggerCore::GetInstance().AddSink(std::move(collector));

    std::atomic<bool> stop(false);
    std::vector<std::thread> writers;
    for (int t = 0; t < 4; ++t) {
        writers.emplace_back([&stop, t]() {
            int i = 0;
            while (!stop.load()) {
                SPDIAG_LOG_ERROR(SpDiag::LogModule::RUNTIME,
                                 "addsink-test t%d i%d", t, i++);
            }
        });
    }

    // 写入进行中添加第二个 sink
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    auto collector2 = std::make_unique<CollectingSink>();
    auto* raw2 = collector2.get();
    SpDiag::LoggerCore::GetInstance().AddSink(std::move(collector2));

    // 继续写入一段时间，让第二个 sink 有机会收到消息
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stop.store(true);
    for (auto& th : writers) {
        th.join();
    }
    SpDiag::LoggerCore::GetInstance().FlushAll();

    // 不崩溃即通过；第二个 sink 应收到一些消息（AddSink 后的）
    EXPECT_GT(raw->Count(), static_cast<size_t>(0));
    EXPECT_GT(raw2->Count(), static_cast<size_t>(0))
        << "新 sink 应在 AddSink 后收到后续消息";
}

// 验证压测后所有消息格式正确，无截断/乱码
TEST_F(LoggerCoreTest, NoMessageCorruption) {
    auto collector = std::make_unique<CollectingSink>();
    auto* raw = collector.get();
    SpDiag::LoggerCore::GetInstance().AddSink(std::move(collector));

    const int num_threads = 8;
    const int per_thread = 500;
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([t, per_thread]() {
            for (int i = 0; i < per_thread; ++i) {
                SPDIAG_LOG_INFO(SpDiag::LogModule::RUNTIME,
                                "corrupt-check t=%d i=%d", t, i);
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    SpDiag::LoggerCore::GetInstance().FlushAll();

    auto msgs = raw->GetMessages();
    ASSERT_GE(msgs.size(), static_cast<size_t>(num_threads * per_thread / 2))
        << "应至少收到大部分消息";

    // 每条消息必须匹配标准格式：[时间] [级别] [模块] [文件:行号] [函数] - 消息
    // 文件名允许 . - _ 等字符（如 .cpp），函数名允许 ()（如 operator()）
    std::regex fmt_re(R"(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\] )"
                      R"(\[(DEBUG|INFO|WARN|ERROR)\] \[\w+\] \[[^\s:\[\]]+:\d+\] \[[^\s\[\]]+\] - .+)");
    for (const auto& m : msgs) {
        EXPECT_TRUE(std::regex_search(m, fmt_re))
            << "消息格式错误或损坏: " << m;
    }
}

// 16 线程 × 5000 条 = 80000 条高压并发测试
// 验证异步日志在高压下的完整性和正确性
TEST_F(LoggerCoreTest, HighConcurrencyStress) {
    auto collector = std::make_unique<CollectingSink>();
    auto* raw = collector.get();
    SpDiag::LoggerCore::GetInstance().AddSink(std::move(collector));

    const int num_threads = 16;
    const int per_thread = 5000;
    const int total = num_threads * per_thread;

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([t, per_thread]() {
            for (int i = 0; i < per_thread; ++i) {
                SPDIAG_LOG_ERROR(SpDiag::LogModule::RUNTIME,
                                 "stress-t%d-i%05d", t, i);
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }

    SpDiag::LoggerCore::GetInstance().FlushAll();

    size_t received = raw->Count();
    size_t dropped = SpDiag::LoggerCore::GetInstance().GetDroppedCount();

    // 队列容量 8192，16×5000=80000 条可能导致部分丢弃
    // 验证：接收数 + 丢弃数 == 总数（或接收数 >= 期望值）
    EXPECT_GE(received + dropped, static_cast<size_t>(total))
        << "接收数 + 丢弃数应至少等于总数";
    EXPECT_GE(received, static_cast<size_t>(total / 2))
        << "应至少收到半数以上消息";
}

// ============================================================================
// 9.6 FormatMessage 格式化
// 注：FormatMessage 是 LoggerCore 私有方法，通过 SPDIAG_LOG_* 宏端到端验证。
// ============================================================================

// 验证异步输出消息格式与设计一致（时间戳、级别、模块、文件:行号、函数名）
TEST_F(LoggerCoreTest, OutputFormat) {
    auto collector = std::make_unique<CollectingSink>();
    auto* raw = collector.get();
    SpDiag::LoggerCore::GetInstance().AddSink(std::move(collector));

    SPDIAG_LOG_INFO(SpDiag::LogModule::SDK, "format-test-message");
    SpDiag::LoggerCore::GetInstance().FlushAll();

    auto msgs = raw->GetMessages();
    ASSERT_EQ(msgs.size(), static_cast<size_t>(1));

    const std::string& m = msgs[0];
    // 期望格式: [2026-01-01 12:00:00.000] [INFO] [SDK] [file.cpp:LINE] [func] - format-test-message
    std::regex fmt_re(R"(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\] )"
                      R"(\[INFO\] \[SDK\] \[\w+\.cpp:\d+\] \[\w+\] - format-test-message)");
    EXPECT_TRUE(std::regex_search(m, fmt_re)) << "格式不匹配: " << m;
}

// 验证超长消息（> 4096）仍包含 ...[TRUNCATED] 标记
TEST_F(LoggerCoreTest, TruncationMarker) {
    auto collector = std::make_unique<CollectingSink>();
    auto* raw = collector.get();
    SpDiag::LoggerCore::GetInstance().AddSink(std::move(collector));

    // 构造超长消息（vsnprintf 缓冲区 4096，需超过此长度才触发截断）
    std::string long_msg(5000, 'X');
    SPDIAG_LOG_INFO(SpDiag::LogModule::SDK, "%s", long_msg.c_str());
    SpDiag::LoggerCore::GetInstance().FlushAll();

    auto msgs = raw->GetMessages();
    ASSERT_EQ(msgs.size(), static_cast<size_t>(1));
    EXPECT_NE(msgs[0].find("...[TRUNCATED]"), std::string::npos)
        << "超长消息应包含截断标记";
}

// 验证 ERROR 级别日志在异步模式下仍输出堆栈
TEST_F(LoggerCoreTest, StackTraceOnError) {
    auto collector = std::make_unique<CollectingSink>();
    auto* raw = collector.get();
    SpDiag::LoggerCore::GetInstance().AddSink(std::move(collector));

    SPDIAG_LOG_ERROR_WITH_STACK(SpDiag::LogModule::SDK, "stack-test-message");
    SpDiag::LoggerCore::GetInstance().FlushAll();

    auto msgs = raw->GetMessages();
    ASSERT_EQ(msgs.size(), static_cast<size_t>(1));

    const std::string& m = msgs[0];
    // 堆栈应在消息体之后，包含 "Stack trace:" 标识
    EXPECT_NE(m.find("stack-test-message"), std::string::npos);
    EXPECT_NE(m.find("Stack trace:"), std::string::npos)
        << "ERROR 级别应输出堆栈";
}

// ============================================================================
// 9.7 ERROR 同步输出（LOG_MODULE_OPTIMIZATION_PLAN.md 方案 B）
// ERROR 级别绕过异步队列，在调用线程直接同步写入 sinks，调用返回前即可见，
// 不因队列满被丢弃。INFO/WARN 等仍走异步队列。
// 注意：仅当 SPDIAG_LOG_ERROR_SYNC=1 时 ERROR 才走同步路径；=0 时回退为
// 全部异步，此时下面 3 个断言同步行为的测试不编译。
// ============================================================================

#if SPDIAG_LOG_ERROR_SYNC

TEST_F(LoggerCoreTest, ErrorWrittenSynchronously) {
    // ERROR 同步路径：SPDIAG_LOG_ERROR 返回后消息应已在 sink 中，
    // 无需 FlushAll / WaitFor（区别于异步路径）。
    auto collector = std::make_unique<CollectingSink>();
    auto* raw = collector.get();
    SpDiag::LoggerCore::GetInstance().AddSink(std::move(collector));

    SPDIAG_LOG_ERROR(SpDiag::LogModule::SDK, "sync-error-%d", 42);

    // 同步写入：调用返回时已落盘到 sink，无需 FlushAll
    auto msgs = raw->GetMessages();
    ASSERT_EQ(msgs.size(), static_cast<size_t>(1));
    EXPECT_NE(msgs[0].find("sync-error-42"), std::string::npos);
}

TEST_F(LoggerCoreTest, ErrorWithStackWrittenSynchronously) {
    auto collector = std::make_unique<CollectingSink>();
    auto* raw = collector.get();
    SpDiag::LoggerCore::GetInstance().AddSink(std::move(collector));

    SPDIAG_LOG_ERROR_WITH_STACK(SpDiag::LogModule::SDK, "sync-stack-error");

    auto msgs = raw->GetMessages();
    ASSERT_EQ(msgs.size(), static_cast<size_t>(1));
    EXPECT_NE(msgs[0].find("sync-stack-error"), std::string::npos);
    EXPECT_NE(msgs[0].find("Stack trace:"), std::string::npos)
        << "ERROR 同步路径应包含堆栈";
}

TEST_F(LoggerCoreTest, ErrorSyncDoesNotDuplicateOnFlush) {
    // ERROR 同步写入后再次 FlushAll 不应产生重复消息。
    auto collector = std::make_unique<CollectingSink>();
    auto* raw = collector.get();
    SpDiag::LoggerCore::GetInstance().AddSink(std::move(collector));

    SPDIAG_LOG_ERROR(SpDiag::LogModule::SDK, "no-duplicate-error");
    ASSERT_EQ(raw->Count(), static_cast<size_t>(1));

    SpDiag::LoggerCore::GetInstance().FlushAll();
    EXPECT_EQ(raw->Count(), static_cast<size_t>(1))
        << "FlushAll 不应重复写入已同步输出的 ERROR";
}

#endif  // SPDIAG_LOG_ERROR_SYNC

TEST_F(LoggerCoreTest, NonErrorStillAsync) {
    // INFO/WARN 仍走异步队列：与 ERROR 同步路径形成对照。
    auto collector = std::make_unique<CollectingSink>();
    auto* raw = collector.get();
    SpDiag::LoggerCore::GetInstance().AddSink(std::move(collector));

    SPDIAG_LOG_INFO(SpDiag::LogModule::SDK, "async-info-message");
    SPDIAG_LOG_WARN(SpDiag::LogModule::SDK, "async-warn-message");

    // 未 FlushAll 前异步消息可能尚未写入，不在此断言；
    // FlushAll 后排空队列，全部可见。
    SpDiag::LoggerCore::GetInstance().FlushAll();
    ASSERT_TRUE(WaitFor([raw] { return raw->Count() >= 2; }))
        << "异步 INFO/WARN 应最终到达 sink";

    auto msgs = raw->GetMessages();
    bool found_info = false;
    bool found_warn = false;
    for (const auto& m : msgs) {
        if (m.find("async-info-message") != std::string::npos) found_info = true;
        if (m.find("async-warn-message") != std::string::npos) found_warn = true;
    }
    EXPECT_TRUE(found_info) << "INFO 异步消息未到达";
    EXPECT_TRUE(found_warn) << "WARN 异步消息未到达";
}
