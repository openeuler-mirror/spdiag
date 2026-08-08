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
#include "logger/sinks.h"

#include <thread>
#include <vector>
#include <fstream>
#include <filesystem>
#include <regex>
#include <atomic>

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

TEST(LogConfigTest, LevelValue) {
    EXPECT_TRUE(SPDIAG_LOG_LEVEL >= 0 && SPDIAG_LOG_LEVEL <= 3);
}

TEST(LogConfigTest, OutputFlags) {
    EXPECT_EQ(SPDIAG_LOG_TO_CONSOLE, 1);
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

private:
    mutable std::mutex mutex_;
    std::vector<std::string> messages_;
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

TEST(MultiThreadTest, HighConcurrencyStress) {
    SpDiag::LoggerCore::GetInstance().ClearSinks();

    const int num_threads = 8;
    const int num_messages = 500;

    auto collector = std::make_unique<CollectingSink>();
    auto* raw_collector = collector.get();
    SpDiag::LoggerCore::GetInstance().AddSink(std::move(collector));

    std::atomic<bool> failed{false};

    auto worker = [&failed, num_messages](int thread_id) {
        try {
            for (int i = 0; i < num_messages; ++i) {
                SPDIAG_LOG_ERROR(SpDiag::LogModule::RUNTIME,
                    "Stress test - Thread %d, iteration %d, value %d",
                    thread_id, i, i * thread_id);
            }
        } catch (...) {
            failed.store(true);
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

    EXPECT_FALSE(failed.load());

    auto messages = raw_collector->GetMessages();
    EXPECT_EQ(messages.size(), static_cast<size_t>(num_threads * num_messages));

    std::regex format_regex(
        R"(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\] )"
        R"(\[ERROR\] \[RUNTIME\] \[.*test_logger\.cpp:\d+\] )"
        R"(\[.*\] - Stress test - Thread \d+, iteration \d+, value -?\d+)");
    for (const auto& msg : messages) {
        EXPECT_TRUE(std::regex_match(msg, format_regex))
            << "Malformed log message: " << msg;
    }
}

TEST(FormatMessageTest, OutputFormat) {
    SpDiag::LoggerCore::GetInstance().ClearSinks();

    auto test_sink = std::make_unique<CollectingSink>();
    auto* raw_sink = test_sink.get();
    SpDiag::LoggerCore::GetInstance().AddSink(std::move(test_sink));

    SPDIAG_LOG_ERROR(SpDiag::LogModule::SDK, "Test format message");

    SpDiag::LoggerCore::GetInstance().FlushAll();

    std::string message = raw_sink->GetLastMessage();
    EXPECT_FALSE(message.empty());

    std::regex format_regex(
        R"(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\] )"
        R"(\[ERROR\] \[SDK\] \[.*test_logger\.cpp:\d+\] )"
        R"(\[.*\] - Test format message)");
    EXPECT_TRUE(std::regex_match(message, format_regex))
        << "Log format mismatch: " << message;
}

TEST(FormatMessageTest, TruncationMarker) {
    SpDiag::LoggerCore::GetInstance().ClearSinks();

    auto test_sink = std::make_unique<CollectingSink>();
    auto* raw_sink = test_sink.get();
    SpDiag::LoggerCore::GetInstance().AddSink(std::move(test_sink));

    std::string long_message(9000, 'x');
    SPDIAG_LOG_ERROR(SpDiag::LogModule::SDK, "%s", long_message.c_str());

    SpDiag::LoggerCore::GetInstance().FlushAll();

    std::string message = raw_sink->GetLastMessage();
    EXPECT_FALSE(message.empty());
    EXPECT_NE(message.find("...[TRUNCATED]"), std::string::npos)
        << "Expected truncation marker not found in: " << message;
}
