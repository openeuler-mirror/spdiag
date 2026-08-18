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

#include <gtest/gtest.h>
#include "spdiag/logger.h"
#include "spdiag/log_macros.h"
#include "spdiag/log_sink.h"
#include "logger/sinks.h"

#include <thread>
#include <vector>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <regex>
#include <atomic>
#include <chrono>

TEST(LogLevelTest, EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(SpDiag::LogLevel::DEBUG), 0u);
    EXPECT_EQ(static_cast<uint8_t>(SpDiag::LogLevel::INFO), 1u);
    EXPECT_EQ(static_cast<uint8_t>(SpDiag::LogLevel::WARN), 2u);
    EXPECT_EQ(static_cast<uint8_t>(SpDiag::LogLevel::ERROR), 3u);
}

TEST(LogModuleTest, EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(SpDiag::LogModule::SDK), 0u);
    EXPECT_EQ(static_cast<uint8_t>(SpDiag::LogModule::PERF_MANAGER), 1u);
    EXPECT_EQ(static_cast<uint8_t>(SpDiag::LogModule::RUNTIME), 2u);
    EXPECT_EQ(static_cast<uint8_t>(SpDiag::LogModule::MANAGER), 3u);
    EXPECT_EQ(static_cast<uint8_t>(SpDiag::LogModule::CLI), 4u);
    EXPECT_EQ(static_cast<uint8_t>(SpDiag::LogModule::PLATFORM), 5u);
    EXPECT_EQ(static_cast<uint8_t>(SpDiag::LogModule::EXAMPLE), 6u);
    EXPECT_EQ(static_cast<uint8_t>(SpDiag::LogModule::UNKNOWN), 255u);
}

TEST(LogLevelTest, LevelNameStrings) {
    EXPECT_STREQ(SpDiag::GetLogLevelName(SpDiag::LogLevel::DEBUG), "DEBUG");
    EXPECT_STREQ(SpDiag::GetLogLevelName(SpDiag::LogLevel::INFO), "INFO");
    EXPECT_STREQ(SpDiag::GetLogLevelName(SpDiag::LogLevel::WARN), "WARN");
    EXPECT_STREQ(SpDiag::GetLogLevelName(SpDiag::LogLevel::ERROR), "ERROR");
    EXPECT_STREQ(SpDiag::GetLogLevelName(static_cast<SpDiag::LogLevel>(99)), "UNKNOWN");
}

TEST(LogModuleTest, ModuleNameStrings) {
    EXPECT_STREQ(SpDiag::GetLogModuleName(SpDiag::LogModule::SDK), "SDK");
    EXPECT_STREQ(SpDiag::GetLogModuleName(SpDiag::LogModule::PERF_MANAGER), "PERF_MANAGER");
    EXPECT_STREQ(SpDiag::GetLogModuleName(SpDiag::LogModule::RUNTIME), "RUNTIME");
    EXPECT_STREQ(SpDiag::GetLogModuleName(SpDiag::LogModule::MANAGER), "MANAGER");
    EXPECT_STREQ(SpDiag::GetLogModuleName(SpDiag::LogModule::CLI), "CLI");
    EXPECT_STREQ(SpDiag::GetLogModuleName(SpDiag::LogModule::PLATFORM), "PLATFORM");
    EXPECT_STREQ(SpDiag::GetLogModuleName(SpDiag::LogModule::EXAMPLE), "EXAMPLE");
    EXPECT_STREQ(SpDiag::GetLogModuleName(SpDiag::LogModule::UNKNOWN), "UNKNOWN");
}

TEST(LogMacroTest, ErrorMacroCompiles) {
    SPDIAG_LOG_ERROR(SpDiag::LogModule::SDK, "Test error message");
}

TEST(LogMacroTest, ErrorWithStackMacroCompiles) {
    SPDIAG_LOG_ERROR_WITH_STACK(SpDiag::LogModule::SDK, "Test error with stack");
}

TEST(LogConfigTest, RuntimeLevelDefaultsToError) {
    auto& logger = SpDiag::LoggerCore::GetInstance();
    // 默认运行时日志级别为 ERROR
    EXPECT_EQ(logger.GetRuntimeLogLevel(), SpDiag::LogLevel::ERROR);
}

TEST(LogConfigTest, OutputFlags) {
    // 构建系统应始终显式定义输出开关为 0 或 1，避免 header 默认值导致行为不一致
    EXPECT_TRUE(SPDIAG_LOG_TO_CONSOLE == 0 || SPDIAG_LOG_TO_CONSOLE == 1);
    EXPECT_TRUE(SPDIAG_LOG_TO_FILE == 0 || SPDIAG_LOG_TO_FILE == 1);
}

TEST(FileSinkTest, Rotation) {
    std::string test_dir = "/tmp/spdiag_log_test";
    std::string test_file = test_dir + "/test.log";
    
    std::filesystem::create_directories(test_dir);
    
    for (int i = 1; i <= 5; ++i) {
        std::filesystem::remove(test_file + "." + std::to_string(i));
    }
    std::filesystem::remove(test_file);

    {
        SpDiag::FileSink sink(test_file, 100, 3);
        
        for (int i = 0; i < 20; ++i) {
            sink.Write("Test message " + std::to_string(i));
        }
        sink.Flush();
    }

    EXPECT_TRUE(std::filesystem::exists(test_file));
    EXPECT_TRUE(std::filesystem::exists(test_file + ".1"));
    EXPECT_TRUE(std::filesystem::exists(test_file + ".2"));
    EXPECT_FALSE(std::filesystem::exists(test_file + ".4"));

    std::filesystem::remove_all(test_dir);
}

TEST(FileSinkTest, RotationOrder) {
    std::string test_dir = "/tmp/spdiag_log_test2";
    std::string test_file = test_dir + "/test.log";
    
    std::filesystem::create_directories(test_dir);
    std::filesystem::remove_all(test_file);
    for (int i = 1; i <= 5; ++i) {
        std::filesystem::remove(test_file + "." + std::to_string(i));
    }

    {
        SpDiag::FileSink sink(test_file, 50, 2);
        
        sink.Write("First batch message");
        sink.Write("First batch message");
        sink.Write("First batch message");
        sink.Flush();
        
        sink.Write("Second batch message");
        sink.Write("Second batch message");
        sink.Write("Second batch message");
        sink.Flush();
    }

    EXPECT_TRUE(std::filesystem::exists(test_file));
    EXPECT_TRUE(std::filesystem::exists(test_file + ".1"));
    EXPECT_TRUE(std::filesystem::exists(test_file + ".2"));
    EXPECT_FALSE(std::filesystem::exists(test_file + ".3"));

    std::filesystem::remove_all(test_dir);
}

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

    std::string GetLastMessage() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return messages_.empty() ? "" : messages_.back();
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

// 适配器 Sink：持有 shared_ptr<CollectingSink>，解决 unique_ptr 不能拷贝的问题
class SharedCollectingSink : public SpDiag::LogSink {
public:
    explicit SharedCollectingSink(std::shared_ptr<CollectingSink> inner)
        : inner_(std::move(inner)) {}
    void Write(const std::string& msg) override { inner_->Write(msg); }
    void Flush() override { inner_->Flush(); }

private:
    std::shared_ptr<CollectingSink> inner_;
};

TEST(MultiThreadTest, ConcurrentWrite) {
    SpDiag::LoggerCore::GetInstance().ClearSinks();

    const int num_threads = 4;
    const int num_messages = 100;

    auto collector = std::make_unique<CollectingSink>();
    auto* raw_collector = collector.get();
    SpDiag::LoggerCore::GetInstance().AddSink(std::move(collector));

    auto worker = [num_messages](int thread_id) {
        for (int i = 0; i < num_messages; ++i) {
            SPDIAG_LOG_ERROR(SpDiag::LogModule::SDK, "Thread %d, message %d", thread_id, i);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    SpDiag::LoggerCore::GetInstance().FlushAll();

    auto messages = raw_collector->GetMessages();
    EXPECT_EQ(messages.size(), static_cast<size_t>(num_threads * num_messages));

    std::regex format_regex(
        R"(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\] )"
        R"(\[ERROR\] \[SDK\] \[.*test_logger\.cpp:\d+\] )"
        R"(\[.*\] - Thread \d+, message \d+)");
    for (const auto& msg : messages) {
        EXPECT_TRUE(std::regex_match(msg, format_regex))
            << "Malformed log message: " << msg;
    }
}

// 注：FormatMessageTest（OutputFormat, TruncationMarker）和
// MultiThreadTest::HighConcurrencyStress 已移除，
// 因为 test_async_logger.cpp 中的 LoggerCoreTest::OutputFormat、
// TruncationMarker、HighConcurrencyStress 已完整覆盖相同场景。

// ========== 运行时日志级别过滤功能测试（从 test_log_level_param.cpp 合并） ==========

using namespace SpDiag;

class LogLevelParamTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto& logger = LoggerCore::GetInstance();
        logger.ClearSinks();
        collector_ = std::make_shared<CollectingSink>();
        logger.AddSink(std::make_unique<SharedCollectingSink>(collector_));
        logger.SetRuntimeLogLevel(LogLevel::DEBUG);
    }

    void TearDown() override {
        auto& logger = LoggerCore::GetInstance();
        logger.FlushAll();
        logger.SetRuntimeLogLevel(LogLevel::ERROR);
        logger.ClearSinks();
    }

    std::shared_ptr<CollectingSink> collector_;
};

// ========== ParseLogLevelName 测试 ==========

TEST(ParseLogLevelNameTest, ValidNames) {
    EXPECT_EQ(ParseLogLevelName("DEBUG"), LogLevel::DEBUG);
    EXPECT_EQ(ParseLogLevelName("INFO"), LogLevel::INFO);
    EXPECT_EQ(ParseLogLevelName("WARN"), LogLevel::WARN);
    EXPECT_EQ(ParseLogLevelName("ERROR"), LogLevel::ERROR);
}

TEST(ParseLogLevelNameTest, CaseInsensitive) {
    EXPECT_EQ(ParseLogLevelName("debug"), LogLevel::DEBUG);
    EXPECT_EQ(ParseLogLevelName("info"), LogLevel::INFO);
    EXPECT_EQ(ParseLogLevelName("warn"), LogLevel::WARN);
    EXPECT_EQ(ParseLogLevelName("error"), LogLevel::ERROR);
    EXPECT_EQ(ParseLogLevelName("Debug"), LogLevel::DEBUG);
    EXPECT_EQ(ParseLogLevelName("ERROR"), LogLevel::ERROR);
}

TEST(ParseLogLevelNameTest, InvalidNames) {
    EXPECT_FALSE(ParseLogLevelName("").has_value());
    EXPECT_FALSE(ParseLogLevelName("TRACE").has_value());
    EXPECT_FALSE(ParseLogLevelName("FATAL").has_value());
    EXPECT_FALSE(ParseLogLevelName("123").has_value());
    EXPECT_FALSE(ParseLogLevelName("debug ").has_value());
}

// ========== SetRuntimeLogLevel / GetRuntimeLogLevel 测试 ==========

TEST_F(LogLevelParamTest, SetAndGetRuntimeLevel) {
    auto& logger = LoggerCore::GetInstance();

    logger.SetRuntimeLogLevel(LogLevel::WARN);
    EXPECT_EQ(logger.GetRuntimeLogLevel(), LogLevel::WARN);

    logger.SetRuntimeLogLevel(LogLevel::DEBUG);
    EXPECT_EQ(logger.GetRuntimeLogLevel(), LogLevel::DEBUG);
}

// ========== 运行时级别过滤功能测试 ==========

TEST_F(LogLevelParamTest, DebugLevelAllLogsPass) {
    auto& logger = LoggerCore::GetInstance();
    logger.SetRuntimeLogLevel(LogLevel::DEBUG);
    collector_->Clear();

    SPDIAG_LOG_DEBUG(LogModule::EXAMPLE, "debug msg");
    SPDIAG_LOG_INFO(LogModule::EXAMPLE, "info msg");
    SPDIAG_LOG_WARN(LogModule::EXAMPLE, "warn msg");
    SPDIAG_LOG_ERROR(LogModule::EXAMPLE, "error msg");

    logger.FlushAll();
    EXPECT_EQ(collector_->Count(), 4u);
}

TEST_F(LogLevelParamTest, InfoLevelFiltersDebug) {
    auto& logger = LoggerCore::GetInstance();
    logger.SetRuntimeLogLevel(LogLevel::INFO);
    collector_->Clear();

    SPDIAG_LOG_DEBUG(LogModule::EXAMPLE, "debug msg");
    SPDIAG_LOG_INFO(LogModule::EXAMPLE, "info msg");
    SPDIAG_LOG_WARN(LogModule::EXAMPLE, "warn msg");
    SPDIAG_LOG_ERROR(LogModule::EXAMPLE, "error msg");

    logger.FlushAll();
    EXPECT_EQ(collector_->Count(), 3u);
}

TEST_F(LogLevelParamTest, WarnLevelFiltersDebugAndInfo) {
    auto& logger = LoggerCore::GetInstance();
    logger.SetRuntimeLogLevel(LogLevel::WARN);
    collector_->Clear();

    SPDIAG_LOG_DEBUG(LogModule::EXAMPLE, "debug msg");
    SPDIAG_LOG_INFO(LogModule::EXAMPLE, "info msg");
    SPDIAG_LOG_WARN(LogModule::EXAMPLE, "warn msg");
    SPDIAG_LOG_ERROR(LogModule::EXAMPLE, "error msg");

    logger.FlushAll();
    EXPECT_EQ(collector_->Count(), 2u);
}

TEST_F(LogLevelParamTest, ErrorLevelFiltersAllButError) {
    auto& logger = LoggerCore::GetInstance();
    logger.SetRuntimeLogLevel(LogLevel::ERROR);
    collector_->Clear();

    SPDIAG_LOG_DEBUG(LogModule::EXAMPLE, "debug msg");
    SPDIAG_LOG_INFO(LogModule::EXAMPLE, "info msg");
    SPDIAG_LOG_WARN(LogModule::EXAMPLE, "warn msg");
    SPDIAG_LOG_ERROR(LogModule::EXAMPLE, "error msg");

    logger.FlushAll();
    EXPECT_EQ(collector_->Count(), 1u);
}

// ========== 验证消息内容正确性 ==========

TEST_F(LogLevelParamTest, FilteredMessagesAreCorrectLevel) {
    auto& logger = LoggerCore::GetInstance();
    logger.SetRuntimeLogLevel(LogLevel::WARN);
    collector_->Clear();

    SPDIAG_LOG_DEBUG(LogModule::EXAMPLE, "should not appear");
    SPDIAG_LOG_INFO(LogModule::EXAMPLE, "should not appear");
    SPDIAG_LOG_WARN(LogModule::EXAMPLE, "should appear warn");
    SPDIAG_LOG_ERROR(LogModule::EXAMPLE, "should appear error");

    logger.FlushAll();
    auto messages = collector_->GetMessages();
    ASSERT_EQ(messages.size(), 2u);

    EXPECT_NE(messages[0].find("[WARN]"), std::string::npos);
    EXPECT_NE(messages[1].find("[ERROR]"), std::string::npos);
}

// ========== 动态切换运行时级别 ==========

TEST_F(LogLevelParamTest, DynamicLevelSwitch) {
    auto& logger = LoggerCore::GetInstance();
    collector_->Clear();

    logger.SetRuntimeLogLevel(LogLevel::DEBUG);
    SPDIAG_LOG_DEBUG(LogModule::EXAMPLE, "debug 1");
    logger.FlushAll();
    EXPECT_EQ(collector_->Count(), 1u);

    collector_->Clear();
    logger.SetRuntimeLogLevel(LogLevel::ERROR);
    SPDIAG_LOG_DEBUG(LogModule::EXAMPLE, "debug 2");
    SPDIAG_LOG_ERROR(LogModule::EXAMPLE, "error 2");
    logger.FlushAll();
    EXPECT_EQ(collector_->Count(), 1u);

    collector_->Clear();
    logger.SetRuntimeLogLevel(LogLevel::DEBUG);
    SPDIAG_LOG_DEBUG(LogModule::EXAMPLE, "debug 3");
    logger.FlushAll();
    EXPECT_EQ(collector_->Count(), 1u);
}

// ========== 线程安全性测试 ==========

TEST_F(LogLevelParamTest, ThreadSafeLevelSwitch) {
    auto& logger = LoggerCore::GetInstance();
    logger.SetRuntimeLogLevel(LogLevel::DEBUG);
    collector_->Clear();

    constexpr int NUM_THREADS = 4;
    constexpr int MSGS_PER_THREAD = 1000;

    std::vector<std::thread> threads;
    std::atomic<bool> stop{false};

    threads.emplace_back([&logger, &stop]() {
        while (!stop.load()) {
            logger.SetRuntimeLogLevel(LogLevel::DEBUG);
            logger.SetRuntimeLogLevel(LogLevel::ERROR);
        }
    });

    for (int t = 0; t < NUM_THREADS - 1; ++t) {
        threads.emplace_back([&logger, &stop, t]() {
            for (int i = 0; i < MSGS_PER_THREAD; ++i) {
                SPDIAG_LOG_INFO(LogModule::EXAMPLE, "thread %d msg %d", t, i);
            }
        });
    }

    for (int t = 1; t < NUM_THREADS; ++t) {
        threads[t].join();
    }
    stop.store(true);
    threads[0].join();

    logger.FlushAll();
    SUCCEED() << "Concurrent level switch + logging completed without crash";
}

// 多线程动态切换日志级别并验证过滤正确性
TEST_F(LogLevelParamTest, ConcurrentLevelSwitchFilterCorrectness) {
    auto& logger = LoggerCore::GetInstance();
    collector_->Clear();

    constexpr int NUM_WRITER_THREADS = 4;
    constexpr int MSGS_PER_THREAD = 2000;

    std::atomic<bool> stop{false};
    std::atomic<int> error_emitted{0};
    std::atomic<int> debug_emitted{0};

    std::vector<std::thread> threads;
    threads.reserve(NUM_WRITER_THREADS + 1);

    threads.emplace_back([&logger, &stop]() {
        bool debug_mode = true;
        while (!stop.load(std::memory_order_relaxed)) {
            logger.SetRuntimeLogLevel(
                debug_mode ? LogLevel::DEBUG : LogLevel::ERROR);
            debug_mode = !debug_mode;
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });

    auto worker = [&](int thread_id) {
        for (int i = 0; i < MSGS_PER_THREAD; ++i) {
            SPDIAG_LOG_ERROR(LogModule::EXAMPLE,
                             "error t=%d i=%d", thread_id, i);
            error_emitted.fetch_add(1, std::memory_order_relaxed);
            SPDIAG_LOG_DEBUG(LogModule::EXAMPLE,
                             "debug t=%d i=%d", thread_id, i);
            debug_emitted.fetch_add(1, std::memory_order_relaxed);
        }
    };
    for (int t = 0; t < NUM_WRITER_THREADS; ++t) {
        threads.emplace_back(worker, t);
    }

    for (int t = 1; t <= NUM_WRITER_THREADS; ++t) {
        threads[t].join();
    }
    stop.store(true);
    threads[0].join();

    logger.FlushAll();

    auto messages = collector_->GetMessages();
    int total_error_emitted = error_emitted.load();
    int total_debug_emitted = debug_emitted.load();
    size_t total_emitted =
        static_cast<size_t>(total_error_emitted) + total_debug_emitted;

    ASSERT_GT(total_error_emitted, 0) << "Should have emitted ERROR logs";
    ASSERT_GT(total_debug_emitted, 0) << "Should have emitted DEBUG logs";
    EXPECT_GE(messages.size(), static_cast<size_t>(total_error_emitted))
        << "All ERROR logs should pass through";
    EXPECT_LE(messages.size(), total_emitted)
        << "Received messages cannot exceed emitted";

    size_t error_received = 0;
    size_t debug_received = 0;
    for (const auto& msg : messages) {
        bool has_error = msg.find("[ERROR]") != std::string::npos;
        bool has_debug = msg.find("[DEBUG]") != std::string::npos;
        ASSERT_TRUE(has_error || has_debug)
            << "Message missing level tag: " << msg;
        ASSERT_FALSE(has_error && has_debug)
            << "Message has conflicting level tags: " << msg;
        if (has_error) {
            ++error_received;
        } else {
            ++debug_received;
        }
    }

    EXPECT_EQ(error_received, static_cast<size_t>(total_error_emitted))
        << "All ERROR logs must pass filter regardless of runtime level";
    EXPECT_LE(debug_received, static_cast<size_t>(total_debug_emitted));
    EXPECT_EQ(error_received + debug_received, messages.size());
}

// ========== LogWithStack 运行时过滤测试 ==========

TEST_F(LogLevelParamTest, LogWithStackRespectsRuntimeLevel) {
    auto& logger = LoggerCore::GetInstance();

    logger.SetRuntimeLogLevel(LogLevel::ERROR);
    collector_->Clear();
    SPDIAG_LOG_ERROR_WITH_STACK(LogModule::EXAMPLE, "error with stack");
    logger.FlushAll();
    EXPECT_EQ(collector_->Count(), 1u);

    collector_->Clear();
    SPDIAG_LOG_WITH_STACK(SPDIAG_LOG_LEVEL_WARN, LogModule::EXAMPLE,
                          "warn with stack");
    logger.FlushAll();
    EXPECT_EQ(collector_->Count(), 0u);

    collector_->Clear();
    logger.SetRuntimeLogLevel(LogLevel::WARN);
    SPDIAG_LOG_WITH_STACK(SPDIAG_LOG_LEVEL_WARN, LogModule::EXAMPLE,
                          "warn with stack 2");
    logger.FlushAll();
    EXPECT_EQ(collector_->Count(), 1u);
}
