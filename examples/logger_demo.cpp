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
#include "spdiag/log_macros.h"

#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <cstdlib>

using namespace SpDiag;

static std::atomic<int> error_count{0};

static void SimulateError() {
    SPDIAG_LOG_ERROR(LogModule::SDK, "Simulated error occurred, count: %d", ++error_count);
}

static void SimulateWarning() {
    SPDIAG_LOG_WARN(LogModule::PERF_MANAGER, "Warning: resource usage high: %d%%", 
                    static_cast<int>(rand() % 100));
}

static void SimulateInfo() {
    SPDIAG_LOG_INFO(LogModule::RUNTIME, "Processing task %d", rand() % 1000);
}

static void SimulateDebug() {
    SPDIAG_LOG_DEBUG(LogModule::MANAGER, "Debug: internal state value = %f", 
                     static_cast<double>(rand()) / RAND_MAX);
}

static void WorkerThread(int thread_id) {
    SPDIAG_LOG_INFO(LogModule::RUNTIME, "Worker thread %d started", thread_id);
    
    for (int i = 0; i < 5; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        switch (rand() % 4) {
            case 0:
                SimulateInfo();
                break;
            case 1:
                SimulateWarning();
                break;
            case 2:
                SimulateDebug();
                break;
            case 3:
                SimulateError();
                break;
        }
    }
    
    SPDIAG_LOG_INFO(LogModule::RUNTIME, "Worker thread %d finished", thread_id);
}

static void TestBasicLogging() {
    std::cout << "\n=== Basic Logging Test ===" << std::endl;
    
    SPDIAG_LOG_DEBUG(LogModule::SDK, "This is a DEBUG message");
    SPDIAG_LOG_INFO(LogModule::SDK, "This is an INFO message");
    SPDIAG_LOG_WARN(LogModule::SDK, "This is a WARN message");
    SPDIAG_LOG_ERROR(LogModule::SDK, "This is an ERROR message");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

static void TestModuleLogging() {
    std::cout << "\n=== Module Logging Test ===" << std::endl;
    
    SPDIAG_LOG_INFO(LogModule::SDK, "SDK module message");
    SPDIAG_LOG_INFO(LogModule::PERF_MANAGER, "Performance Manager module message");
    SPDIAG_LOG_INFO(LogModule::RUNTIME, "Runtime module message");
    SPDIAG_LOG_INFO(LogModule::MANAGER, "Manager module message");
    SPDIAG_LOG_INFO(LogModule::CLI, "CLI module message");
    SPDIAG_LOG_INFO(LogModule::PLATFORM, "Platform module message");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

static void TestStackTrace() {
    std::cout << "\n=== Stack Trace Test ===" << std::endl;
    
    SPDIAG_LOG_ERROR_WITH_STACK(LogModule::SDK, "Error with stack trace - test 1");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    SPDIAG_LOG_ERROR_WITH_STACK(LogModule::PERF_MANAGER, "Error with stack trace - test 2");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

static void TestThreadSafety() {
    std::cout << "\n=== Thread Safety Test ===" << std::endl;
    
    const int num_threads = 4;
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(WorkerThread, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

static void TestFormatting() {
    std::cout << "\n=== Formatting Test ===" << std::endl;
    
    SPDIAG_LOG_INFO(LogModule::SDK, "Integer: %d, Float: %.2f, String: %s", 
                    42, 3.14159, "hello");
    SPDIAG_LOG_WARN(LogModule::PERF_MANAGER, "Memory usage: %zu bytes, CPU: %.1f%%", 
                    1024 * 1024, 75.5);
    SPDIAG_LOG_ERROR(LogModule::RUNTIME, "Error code: 0x%X, Line: %d", 
                    0xDEADBEEF, __LINE__);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

static void TestHighVolumeLogging() {
    std::cout << "\n=== High Volume Logging Test ===" << std::endl;
    
    const int num_logs = 100;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_logs; ++i) {
        SPDIAG_LOG_INFO(LogModule::SDK, "Log message %d/%d", i + 1, num_logs);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Logged " << num_logs << " messages in " << duration.count() << " ms" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

int main() {
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    
    std::cout << "========================================" << std::endl;
    std::cout << "  SpDiag Logger Demo" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Log Level: " << GetLogLevelName(static_cast<LogLevel>(SPDIAG_LOG_LEVEL)) << std::endl;
    std::cout << "Console Output: " << (SPDIAG_LOG_TO_CONSOLE ? "Enabled" : "Disabled") << std::endl;
    std::cout << "File Output: " << (SPDIAG_LOG_TO_FILE ? "Enabled" : "Disabled") << std::endl;
    
    if (SPDIAG_LOG_TO_FILE) {
        std::cout << "Log File: " << SPDIAG_LOG_FILE_PATH << std::endl;
        std::cout << "Max Size: " << SPDIAG_LOG_MAX_SIZE << " bytes" << std::endl;
        std::cout << "Max Files: " << SPDIAG_LOG_MAX_FILES << std::endl;
    }
    std::cout << "========================================" << std::endl;
    
    try {
        TestBasicLogging();
        TestModuleLogging();
        TestStackTrace();
        TestThreadSafety();
        TestFormatting();
        TestHighVolumeLogging();
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "  All tests completed successfully!" << std::endl;
        std::cout << "========================================" << std::endl;
        
        return 0;
    } catch (const std::exception& e) {
        SPDIAG_LOG_ERROR(LogModule::SDK, "Exception caught: %s", e.what());
        return 1;
    }
}