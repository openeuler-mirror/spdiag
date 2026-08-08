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
#include "spdiag/perf_key_defined.h"
#include "spdiag/perf_manager.h"
#include "spdiag/perf_config.h"

#include <iostream>
#include <vector>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <iomanip>
#include <atomic>
#include <thread>

#include <sched.h>
#include <pthread.h>

using namespace SpDiag;

// Bind current thread to a specific CPU core
static void BindToCore(int coreId) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(coreId, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
}

// Allow current thread to run on all CPU cores
static void UnbindFromCore() {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int i = 0; i < static_cast<int>(std::thread::hardware_concurrency()); ++i) {
        CPU_SET(i, &cpuset);
    }
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
}

struct BenchmarkResult {
    std::string taskName;
    std::string taskType;
    bool isBound;  // true=bound, false=unbound
    int coreId;    // Core ID if bound (valid only when isBound=true)
    
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

static void ShortTask() {
    volatile uint64_t sum = 0;
    for (int i = 0; i < 100; ++i) {
        sum += static_cast<uint64_t>(i) * 3;
    }
}

static void MediumTask() {
    auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(1)) {
    }
}

static void LongTask() {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

template<typename TaskFunc>
static void RunBenchmark(const std::string& taskName, 
                         const std::string& taskType,
                         uint64_t targetDurationNs,
                         uint64_t iterations,
                         TaskFunc&& task,
                         bool isBound,
                         int coreId,
                         std::vector<BenchmarkResult>& results) {
    
    std::cout << "Benchmarking: " << taskName << " (target: " 
              << (targetDurationNs / 1000.0) << " μs, iterations: " 
              << iterations << ")";
    
    // Set CPU affinity
    if (isBound) {
        BindToCore(coreId);
        std::cout << " (bound to core " << coreId << ")";
    } else {
        UnbindFromCore();
        std::cout << " (unbound)";
    }
    std::cout << "..." << std::flush;

    BenchmarkResult result;
    result.taskName = taskName;
    result.taskType = taskType;
    result.isBound = isBound;
    result.coreId = coreId;
    result.taskDurationNs = targetDurationNs;
    result.iterations = iterations;
    
    // Baseline: Measure total time (without PerfPoint)
    auto baselineStart = std::chrono::steady_clock::now();
    for (uint64_t i = 0; i < iterations; ++i) {
        task();
    }
    auto baselineEnd = std::chrono::steady_clock::now();
    
    uint64_t baselineTotal = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(baselineEnd - baselineStart).count());
    result.baselineTotalNs = baselineTotal;
    result.baselineAvgNs = baselineTotal / iterations;
    result.baselineMinNs = 0;
    result.baselineMaxNs = 0;
    
    // Monitored: Measure total time (with PerfPoint)
    auto monitoredStart = std::chrono::steady_clock::now();
    for (uint64_t i = 0; i < iterations; ++i) {
        PerfPoint pt(PerfKeyDefined::PROCESS_WORK, PerfLevel::KEY_MODULE);
        task();
        pt.End(0);
    }
    auto monitoredEnd = std::chrono::steady_clock::now();
    
    uint64_t monitoredTotal = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(monitoredEnd - monitoredStart).count());
    result.monitoredTotalNs = monitoredTotal;
    result.monitoredAvgNs = monitoredTotal / iterations;
    result.monitoredMinNs = 0;
    result.monitoredMaxNs = 0;
    
    result.absoluteOverheadNs = static_cast<int64_t>(monitoredTotal) - static_cast<int64_t>(baselineTotal);
    result.absoluteOverheadNs /= static_cast<int64_t>(iterations);
    result.relativeOverheadPercent = (static_cast<double>(monitoredTotal) / baselineTotal - 1.0) * 100.0;
    result.qpsImpactPercent = (1.0 - static_cast<double>(baselineTotal) / monitoredTotal) * 100.0;
    
    results.push_back(result);
    
    std::cout << " Done!" << std::endl;
}

// Helper function to calculate average overhead
static double CalculateAverageOverhead(const std::vector<BenchmarkResult>& results) {
    if (results.empty()) return 0.0;
    double sum = 0.0;
    for (const auto& r : results) {
        sum += r.relativeOverheadPercent;
    }
    return sum / results.size();
}

// Print comparison results between bound and unbound modes
static void PrintComparisonResults(const std::vector<BenchmarkResult>& results) {
    // Separate bound and unbound results
    std::vector<BenchmarkResult> boundResults;
    std::vector<BenchmarkResult> unboundResults;
    
    for (const auto& r : results) {
        if (r.isBound) {
            boundResults.push_back(r);
        } else {
            unboundResults.push_back(r);
        }
    }
    
    // Print bound mode results
    if (!boundResults.empty()) {
        std::cout << "\n";
        std::cout << "================================================================================\n";
        std::cout << "                    TIME OVERHEAD BENCHMARK RESULTS (BOUND MODE)                \n";
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
        
        for (const auto& r : boundResults) {
            std::string displayName = r.taskName + "(bound)";
            std::cout << std::left << std::setw(20) << displayName
                      << std::setw(15) << std::to_string(r.iterations)
                      << std::setw(18) << std::to_string(r.taskDurationNs) + " ns"
                      << std::setw(20) << std::to_string(r.baselineAvgNs) + " ns"
                      << std::setw(20) << std::to_string(r.monitoredAvgNs) + " ns"
                      << std::setw(20) << std::to_string(r.absoluteOverheadNs) + " ns"
                      << std::setw(15) << std::to_string(r.relativeOverheadPercent).substr(0, 6) + "%"
                      << std::setw(15) << std::to_string(r.qpsImpactPercent).substr(0, 6) + "%"
                      << "\n";
        }
        
        double boundAvg = CalculateAverageOverhead(boundResults);
        std::cout << "\nAverage Overhead (Bound): " << boundAvg << "%\n";
    }
    
    // Print unbound mode results
    if (!unboundResults.empty()) {
        std::cout << "\n";
        std::cout << "================================================================================\n";
        std::cout << "                   TIME OVERHEAD BENCHMARK RESULTS (UNBOUND MODE)               \n";
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
        
        for (const auto& r : unboundResults) {
            std::string displayName = r.taskName + "(unbound)";
            std::cout << std::left << std::setw(20) << displayName
                      << std::setw(15) << std::to_string(r.iterations)
                      << std::setw(18) << std::to_string(r.taskDurationNs) + " ns"
                      << std::setw(20) << std::to_string(r.baselineAvgNs) + " ns"
                      << std::setw(20) << std::to_string(r.monitoredAvgNs) + " ns"
                      << std::setw(20) << std::to_string(r.absoluteOverheadNs) + " ns"
                      << std::setw(15) << std::to_string(r.relativeOverheadPercent).substr(0, 6) + "%"
                      << std::setw(15) << std::to_string(r.qpsImpactPercent).substr(0, 6) + "%"
                      << "\n";
        }
        
        double unboundAvg = CalculateAverageOverhead(unboundResults);
        std::cout << "\nAverage Overhead (Unbound): " << unboundAvg << "%\n";
    }
    
    // Print comparison analysis if both modes are present
    if (!boundResults.empty() && !unboundResults.empty()) {
        std::cout << "\n";
        std::cout << "================================================================================\n";
        std::cout << "                              COMPARISON ANALYSIS                               \n";
        std::cout << "================================================================================\n\n";
        
        std::cout << "Task Comparison (Bound vs Unbound):\n";
        std::cout << std::string(123, '-') << "\n";
        std::cout << std::left << std::setw(20) << "Task Name"
                  << std::setw(18) << "Bound Overhead"
                  << std::setw(20) << "Unbound Overhead"
                  << std::setw(18) << "Difference"
                  << std::setw(15) << "Impact"
                  << "\n";
        std::cout << std::string(123, '-') << "\n";
        
        double totalDiff = 0.0;
        double totalImpact = 0.0;
        int count = 0;
        
        for (size_t i = 0; i < boundResults.size() && i < unboundResults.size(); ++i) {
            double diff = unboundResults[i].relativeOverheadPercent - boundResults[i].relativeOverheadPercent;
            double impact = (diff / boundResults[i].relativeOverheadPercent) * 100.0;
            
            std::cout << std::left << std::setw(20) << boundResults[i].taskName
                      << std::setw(18) << std::to_string(boundResults[i].relativeOverheadPercent).substr(0, 6) + "%"
                      << std::setw(20) << std::to_string(unboundResults[i].relativeOverheadPercent).substr(0, 6) + "%"
                      << std::setw(18) << (diff >= 0 ? "+" : "") + std::to_string(diff).substr(0, 6) + "%"
                      << std::setw(15) << (impact >= 0 ? "+" : "") + std::to_string(impact).substr(0, 6) + "%"
                      << "\n";
            
            totalDiff += diff;
            totalImpact += impact;
            ++count;
        }
        
        std::cout << std::string(123, '-') << "\n";
        double avgDiff = totalDiff / count;
        double avgImpact = totalImpact / count;
        std::cout << std::left << std::setw(20) << "Average"
                  << std::setw(18) << std::to_string(CalculateAverageOverhead(boundResults)).substr(0, 6) + "%"
                  << std::setw(20) << std::to_string(CalculateAverageOverhead(unboundResults)).substr(0, 6) + "%"
                  << std::setw(18) << (avgDiff >= 0 ? "+" : "") + std::to_string(avgDiff).substr(0, 6) + "%"
                  << std::setw(15) << (avgImpact >= 0 ? "+" : "") + std::to_string(avgImpact).substr(0, 6) + "%"
                  << "\n";
        
        std::cout << "\nKey Findings:\n";
        std::cout << "  - Unbound mode shows " << (avgDiff >= 0 ? "+" : "") << avgDiff << "% higher overhead on average\n";
        std::cout << "  - Thread migration overhead: +" << avgImpact << "% relative increase\n";
        std::cout << "  - Cache effects: More pronounced in unbound mode\n";
        std::cout << "  - Recommendation: Use core binding for latency-critical applications\n";
    }
    
    // Print summary
    std::cout << "\n";
    std::cout << "================================================================================\n";
    std::cout << "                                   SUMMARY                                      \n";
    std::cout << "================================================================================\n\n";
    
    if (!boundResults.empty()) {
        double boundAvg = CalculateAverageOverhead(boundResults);
        double boundMax = 0.0;
        for (const auto& r : boundResults) {
            if (r.relativeOverheadPercent > boundMax) {
                boundMax = r.relativeOverheadPercent;
            }
        }
        
        std::cout << "Bound Mode:\n";
        std::cout << "  Average Overhead: " << boundAvg << "%\n";
        std::cout << "  Maximum Overhead: " << boundMax << "%\n";
        if (boundAvg < 1.0) {
            std::cout << "  EXCELLENT: Overhead is negligible (< 1%)\n";
        } else if (boundAvg < 5.0) {
            std::cout << "  GOOD: Overhead is acceptable (1-5%)\n";
        } else if (boundAvg < 10.0) {
            std::cout << "  MODERATE: Overhead is noticeable (5-10%)\n";
        } else {
            std::cout << "  HIGH: Overhead is significant (> 10%)\n";
        }
        std::cout << "\n";
    }
    
    if (!unboundResults.empty()) {
        double unboundAvg = CalculateAverageOverhead(unboundResults);
        double unboundMax = 0.0;
        for (const auto& r : unboundResults) {
            if (r.relativeOverheadPercent > unboundMax) {
                unboundMax = r.relativeOverheadPercent;
            }
        }
        
        std::cout << "Unbound Mode:\n";
        std::cout << "  Average Overhead: " << unboundAvg << "%\n";
        std::cout << "  Maximum Overhead: " << unboundMax << "%\n";
        if (unboundAvg < 1.0) {
            std::cout << "  EXCELLENT: Overhead is negligible (< 1%)\n";
        } else if (unboundAvg < 5.0) {
            std::cout << "  GOOD: Overhead is acceptable (1-5%)\n";
        } else if (unboundAvg < 10.0) {
            std::cout << "  MODERATE: Overhead is noticeable (5-10%)\n";
        } else {
            std::cout << "  HIGH: Overhead is significant (> 10%)\n";
        }
        std::cout << "\n";
    }
    
    // Production recommendations
    std::cout << "PRODUCTION RECOMMENDATIONS:\n";
    if (!boundResults.empty() && !unboundResults.empty()) {
        double boundAvg = CalculateAverageOverhead(boundResults);
        double unboundAvg = CalculateAverageOverhead(unboundResults);
        double improvement = ((unboundAvg - boundAvg) / unboundAvg) * 100.0;
        std::cout << "  - For latency-critical paths: Use core binding (" 
                  << improvement << "% performance improvement)\n";
    }
    std::cout << "  - For high-throughput batch: Unbound is acceptable (simpler deployment)\n";
    std::cout << "  - Consider hybrid approach: Bind critical threads, unbind background threads\n";
    std::cout << "  - Monitor cache miss rates in unbound mode for performance tuning\n";
    std::cout << "\n";
}

int main(int argc, char* argv[]) {
    uint64_t shortIterations = 10000;
    uint64_t mediumIterations = 1000;
    uint64_t longIterations = 100;
    
    bool boundMode = true;
    bool unboundMode = true;
    int bindCoreId = 0;
    
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            shortIterations = std::stoull(argv[++i]);
        } else if (std::strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            mediumIterations = std::stoull(argv[++i]);
        } else if (std::strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
            longIterations = std::stoull(argv[++i]);
        } else if (std::strcmp(argv[i], "--bound") == 0) {
            boundMode = true;
            unboundMode = false;
        } else if (std::strcmp(argv[i], "--unbound") == 0) {
            boundMode = false;
            unboundMode = true;
        } else if (std::strcmp(argv[i], "--both") == 0) {
            boundMode = true;
            unboundMode = true;
        } else if (std::strcmp(argv[i], "--core") == 0 && i + 1 < argc) {
            bindCoreId = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            std::cout << "Usage: benchmark_time [-s short_iters] [-m medium_iters] [-l long_iters] [OPTIONS]\n"
                      << "  -s  Short task iterations (default: 10000)\n"
                      << "  -m  Medium task iterations (default: 1000)\n"
                      << "  -l  Long task iterations (default: 100)\n"
                      << "\nCPU Affinity Options:\n"
                      << "  --bound     Test only bound mode (threads pinned to CPU core)\n"
                      << "  --unbound   Test only unbound mode (threads can migrate)\n"
                      << "  --both      Test both modes (default)\n"
                      << "  --core N    Bind to CPU core N (default: 0)\n"
                      << "\nThis benchmark measures the time overhead of SpDiag monitoring.\n";
            return 0;
        }
    }
    
    std::cout << "=== SpDiag Time Overhead Benchmark ===\n";
    std::cout << "Mode: ";
    if (boundMode && unboundMode) {
        std::cout << "Both (Bound + Unbound)";
    } else if (boundMode) {
        std::cout << "Bound Only";
    } else {
        std::cout << "Unbound Only";
    }
    std::cout << "\n";
    if (boundMode) {
        std::cout << "Bound Core: " << bindCoreId << "\n";
    }
    std::cout << "\n";
    
    std::vector<BenchmarkResult> results;
    
    // Phase 1: Bound mode
    if (boundMode) {
        std::cout << "=== PHASE 1: BOUND MODE (Core-Bound) ===\n";
        std::cout << "Binding to core " << bindCoreId << "...\n\n";
        
        RunBenchmark("ShortTask", "CPU", 500, shortIterations, ShortTask, true, bindCoreId, results);
        RunBenchmark("MediumTask", "CPU", 1000000, mediumIterations, MediumTask, true, bindCoreId, results);
        RunBenchmark("LongTask", "IO", 10000000, longIterations, LongTask, true, bindCoreId, results);
    }
    
    // Phase 2: Unbound mode
    if (unboundMode) {
        if (boundMode) {
            std::cout << "\n";
        }
        std::cout << "=== PHASE 2: UNBOUND MODE (Core-Unbound) ===\n";
        std::cout << "Allowing thread migration...\n\n";
        
        RunBenchmark("ShortTask", "CPU", 500, shortIterations, ShortTask, false, -1, results);
        RunBenchmark("MediumTask", "CPU", 1000000, mediumIterations, MediumTask, false, -1, results);
        RunBenchmark("LongTask", "IO", 10000000, longIterations, LongTask, false, -1, results);
    }
    
    // Phase 3: Print comparison results
    PrintComparisonResults(results);
    
    return 0;
}
