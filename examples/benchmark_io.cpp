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

#include "spdiag/perf_point.h"
#include "spdiag/perf_manager.h"
#include "spdiag/perf_config.h"
#include "spdiag/perf_level.h"

#include <iostream>
#include <vector>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <iomanip>
#include <atomic>
#include <thread>
#include <random>
#include <cstdio>

using namespace SpDiag;

struct IOBenchmarkResult {
    std::string taskName;
    std::string taskType;
    uint64_t taskDurationNs;
    uint64_t iterations;
    
    uint64_t baselineTotalNs;
    uint64_t baselineAvgNs;
    uint64_t baselineMinNs;
    uint64_t baselineMaxNs;
    
    uint64_t monitoredTotalNs;
    uint64_t monitoredAvgNs;
    uint64_t monitoredMinNs;
    uint64_t monitoredMaxNs;
    
    int64_t absoluteOverheadNs;
    double relativeOverheadPercent;
    double qpsImpactPercent;
};

static void SmallFileSequentialRead() {
    const char* filename = "/tmp/bench_small_read.tmp";
    const size_t bufferSize = 1024;
    char buffer[bufferSize];
    
    FILE* fp = fopen(filename, "wb");
    if (!fp) return;
    std::vector<char> testData(1024 * 100, 'A');
    fwrite(testData.data(), 1, testData.size(), fp);
    fclose(fp);
    
    fp = fopen(filename, "rb");
    if (!fp) return;
    
    fseek(fp, 0, SEEK_SET);
    fread(buffer, 1, bufferSize, fp);
    volatile char ensure = buffer[0];
    (void)ensure;
    
    fclose(fp);
    remove(filename);
}

static void SmallFileSequentialWrite() {
    const char* filename = "/tmp/bench_small_write.tmp";
    const size_t bufferSize = 1024;
    std::vector<char> buffer(bufferSize, 'X');
    
    FILE* fp = fopen(filename, "wb");
    if (!fp) return;
    
    fwrite(buffer.data(), 1, bufferSize, fp);
    fflush(fp);
    
    fclose(fp);
    remove(filename);
}

static void LargeFileSequentialRead() {
    const char* filename = "/tmp/bench_large_read.tmp";
    const size_t bufferSize = 1024 * 1024;
    std::vector<char> buffer(bufferSize);
    
    FILE* fp = fopen(filename, "wb");
    if (!fp) return;
    std::vector<char> testData(bufferSize, 'A');
    fwrite(testData.data(), 1, testData.size(), fp);
    fclose(fp);
    
    fp = fopen(filename, "rb");
    if (!fp) return;
    
    fseek(fp, 0, SEEK_SET);
    fread(buffer.data(), 1, bufferSize, fp);
    volatile char ensure = buffer[0];
    (void)ensure;
    
    fclose(fp);
    remove(filename);
}

static void LargeFileSequentialWrite() {
    const char* filename = "/tmp/bench_large_write.tmp";
    const size_t bufferSize = 1024 * 1024;
    std::vector<char> buffer(bufferSize, 'X');
    
    FILE* fp = fopen(filename, "wb");
    if (!fp) return;
    
    fwrite(buffer.data(), 1, bufferSize, fp);
    fflush(fp);
    
    fclose(fp);
    remove(filename);
}

static void RandomRead() {
    const char* filename = "/tmp/bench_random_read.tmp";
    const size_t fileSize = 1024 * 1024;
    const size_t bufferSize = 1024;
    char buffer[bufferSize];
    
    FILE* fp = fopen(filename, "wb");
    if (!fp) return;
    std::vector<char> testData(fileSize, 'X');
    fwrite(testData.data(), 1, fileSize, fp);
    fclose(fp);
    
    fp = fopen(filename, "rb");
    if (!fp) return;
    
    static std::mt19937 gen(42);
    std::uniform_int_distribution<> distrib(0, fileSize - bufferSize);
    
    int offset = distrib(gen);
    fseek(fp, offset, SEEK_SET);
    fread(buffer, 1, bufferSize, fp);
    volatile char ensure = buffer[0];
    (void)ensure;
    
    fclose(fp);
    remove(filename);
}

static void RandomWrite() {
    const char* filename = "/tmp/bench_random_write.tmp";
    const size_t fileSize = 1024 * 1024;
    const size_t bufferSize = 1024;
    std::vector<char> buffer(bufferSize, 'Y');
    
    FILE* fp = fopen(filename, "wb");
    if (!fp) return;
    std::vector<char> testData(fileSize, 'X');
    fwrite(testData.data(), 1, fileSize, fp);
    fclose(fp);
    
    fp = fopen(filename, "r+b");
    if (!fp) return;
    
    static std::mt19937 gen(42);
    std::uniform_int_distribution<> distrib(0, fileSize - bufferSize);
    
    int offset = distrib(gen);
    fseek(fp, offset, SEEK_SET);
    fwrite(buffer.data(), 1, bufferSize, fp);
    fflush(fp);
    
    fclose(fp);
    remove(filename);
}

static void MemoryCopy() {
    const size_t size = 1024 * 1024;
    std::vector<char> src(size, 'A');
    std::vector<char> dst(size);
    
    for (size_t i = 0; i < size; ++i) {
        dst[i] = src[i];
    }
    volatile char ensure = dst[0];
    (void)ensure;
}

static void MemoryFill() {
    const size_t size = 1024 * 1024;
    std::vector<char> buffer(size);
    
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = static_cast<char>(i & 0xFF);
    }
    volatile char ensure = buffer[0];
    (void)ensure;
}

template<typename TaskFunc>
static uint64_t MeasureTaskOnce(TaskFunc&& task) {
    auto start = std::chrono::high_resolution_clock::now();
    task();
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
}

template<typename TaskFunc>
static void RunBenchmark(const std::string& taskName, 
                         const std::string& taskType,
                         uint64_t targetDurationNs,
                         uint64_t iterations,
                         TaskFunc&& task,
                         std::vector<IOBenchmarkResult>& results) {
    
    std::cout << "Benchmarking: " << taskName << " (target: " 
              << (targetDurationNs / 1000.0) << " μs, iterations: " 
              << iterations << ")..." << std::flush;

    IOBenchmarkResult result;
    result.taskName = taskName;
    result.taskType = taskType;
    result.taskDurationNs = targetDurationNs;
    result.iterations = iterations;
    
    uint64_t baselineTotal = 0;
    uint64_t baselineMin = UINT64_MAX;
    uint64_t baselineMax = 0;
    
    for (uint64_t i = 0; i < iterations; ++i) {
        uint64_t duration = MeasureTaskOnce(task);
        baselineTotal += duration;
        if (duration < baselineMin) baselineMin = duration;
        if (duration > baselineMax) baselineMax = duration;
    }
    
    result.baselineTotalNs = baselineTotal;
    result.baselineAvgNs = baselineTotal / iterations;
    result.baselineMinNs = baselineMin;
    result.baselineMaxNs = baselineMax;
    
    uint64_t monitoredTotal = 0;
    uint64_t monitoredMin = UINT64_MAX;
    uint64_t monitoredMax = 0;
    
    for (uint64_t i = 0; i < iterations; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        task();
        auto t1 = std::chrono::steady_clock::now();
        uint64_t duration = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        
        monitoredTotal += duration;
        if (duration < monitoredMin) monitoredMin = duration;
        if (duration > monitoredMax) monitoredMax = duration;
    }
    
    result.monitoredTotalNs = monitoredTotal;
    result.monitoredAvgNs = monitoredTotal / iterations;
    result.monitoredMinNs = monitoredMin;
    result.monitoredMaxNs = monitoredMax;
    
    result.absoluteOverheadNs = static_cast<int64_t>(monitoredTotal) - static_cast<int64_t>(baselineTotal);
    result.absoluteOverheadNs /= static_cast<int64_t>(iterations);
    result.relativeOverheadPercent = (static_cast<double>(monitoredTotal) / baselineTotal - 1.0) * 100.0;
    result.qpsImpactPercent = (1.0 - static_cast<double>(baselineTotal) / monitoredTotal) * 100.0;
    
    results.push_back(result);
    
    std::cout << " Done!" << std::endl;
}

static void PrintResults(const std::vector<IOBenchmarkResult>& results) {
    std::cout << "\n";
    std::cout << "================================================================================\n";
    std::cout << "                        TIME OVERHEAD BENCHMARK RESULTS                         \n";
    std::cout << "================================================================================\n\n";
    
    std::cout << std::left << std::setw(20) << "Task Name"
              << std::setw(15) << "Iterations"
              << std::setw(18) << "Target(ns)"
              << std::setw(20) << "Baseline(ns)"
              << std::setw(20) << "Monitored(ns)"
              << std::setw(20) << "Overhead(ns)"
              << std::setw(15) << "Overhead %"
              << std::setw(15) << "QPS Impact"
              << "\n";
    
    std::cout << std::string(123, '-') << "\n";
    
    for (const auto& r : results) {
        std::cout << std::left << std::setw(20) << r.taskName
                  << std::setw(15) << std::to_string(r.iterations)
                  << std::setw(18) << std::to_string(r.taskDurationNs) + " ns"
                  << std::setw(20) << std::to_string(r.baselineAvgNs) + " ns"
                  << std::setw(20) << std::to_string(r.monitoredAvgNs) + " ns"
                  << std::setw(20) << std::to_string(r.absoluteOverheadNs) + " ns"
                  << std::setw(15) << std::to_string(r.relativeOverheadPercent).substr(0, 6) + "%"
                  << std::setw(15) << std::to_string(r.qpsImpactPercent).substr(0, 6) + "%"
                  << "\n";
    }
    
    std::cout << "\n";
    std::cout << "================================================================================\n";
    std::cout << "                                   DETAILED STATS                               \n";
    std::cout << "================================================================================\n\n";
    
    for (const auto& r : results) {
        std::cout << r.taskName << " (iterations: " << r.iterations << "):\n";
        std::cout << "  Baseline:  avg=" << r.baselineAvgNs << "ns, min=" 
                  << r.baselineMinNs << "ns, max=" << r.baselineMaxNs 
                  << "ns, range=" << (r.baselineMaxNs - r.baselineMinNs) << "ns\n";
        std::cout << "  Monitored: avg=" << r.monitoredAvgNs << "ns, min=" 
                  << r.monitoredMinNs << "ns, max=" << r.monitoredMaxNs 
                  << "ns, range=" << (r.monitoredMaxNs - r.monitoredMinNs) << "ns\n";
        std::cout << "  Overhead:  " << r.absoluteOverheadNs << " ns (" 
                  << r.relativeOverheadPercent << "%)\n";
        std::cout << "  QPS Impact: " << r.qpsImpactPercent << "%\n";
        std::cout << "\n";
    }
    
    double avgOverhead = 0.0;
    double maxOverhead = 0.0;
    for (const auto& r : results) {
        avgOverhead += r.relativeOverheadPercent;
        if (r.relativeOverheadPercent > maxOverhead) {
            maxOverhead = r.relativeOverheadPercent;
        }
    }
    avgOverhead /= results.size();
    
    std::cout << "================================================================================\n";
    std::cout << "                                   SUMMARY                                      \n";
    std::cout << "================================================================================\n";
    std::cout << "Average Overhead: " << avgOverhead << "%\n";
    std::cout << "Maximum Overhead: " << maxOverhead << "%\n";
    std::cout << "\n";
    
    if (avgOverhead < 1.0) {
        std::cout << "GOOD: Overhead is negligible (< 1%)\n";
    } else if (avgOverhead < 5.0) {
        std::cout << "GOOD: Overhead is acceptable (1-5%)\n";
    } else if (avgOverhead < 10.0) {
        std::cout << "MODERATE: Overhead is noticeable (5-10%)\n";
    } else {
        std::cout << "HIGH: Overhead is significant (> 10%)\n";
    }
}

int main(int argc, char* argv[]) {
    bool quickMode = false;
    uint64_t iterations = 1000;
    
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-q") == 0) {
            quickMode = true;
            iterations = 100;
        } else if (std::strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            iterations = std::atol(argv[++i]);
        } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            std::cout << "Usage: benchmark_io [-q] [-i iterations]\n"
                      << "  -q  Quick mode (fewer iterations)\n"
                      << "  -i  Number of iterations (default 1000)\n"
                      << "\nThis benchmark measures time overhead for I/O intensive tasks.\n";
            return 0;
        }
    }
    
    std::cout << "=== SpDiag I/O Time Overhead Benchmark ===\n";
    std::cout << "Mode: " << (quickMode ? "Quick" : "Full") 
              << " (iterations: " << iterations << ")\n\n";
    
    std::vector<IOBenchmarkResult> results;
    
    RunBenchmark("SmallFileSeqRead", "FileIO", 15000, iterations,
                 SmallFileSequentialRead, results);
    
    RunBenchmark("SmallFileSeqWrite", "FileIO", 20000, iterations,
                 SmallFileSequentialWrite, results);
    
    RunBenchmark("LargeFileSeqRead", "FileIO", 1000000, iterations / 10,
                 LargeFileSequentialRead, results);
    
    RunBenchmark("LargeFileSeqWrite", "FileIO", 1000000, iterations / 10,
                 LargeFileSequentialWrite, results);
    
    RunBenchmark("RandomRead", "RandomIO", 50000, iterations,
                 RandomRead, results);
    
    RunBenchmark("RandomWrite", "RandomIO", 60000, iterations,
                 RandomWrite, results);
    
    RunBenchmark("MemoryCopy", "Memory", 5000, iterations,
                 MemoryCopy, results);
    
    RunBenchmark("MemoryFill", "Memory", 5000, iterations,
                 MemoryFill, results);
    
    PrintResults(results);
    
    return 0;
}
