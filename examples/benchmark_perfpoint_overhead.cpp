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
#include <chrono>
#include <cstring>
#include <iomanip>

using namespace SpDiag;

int main(int argc, char* argv[]) {
    uint64_t iterations = 1000000;
    
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            iterations = std::stoull(argv[++i]);
        } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            std::cout << "Usage: benchmark_perfpoint_overhead [-n iterations]\n"
                      << "  -n  Number of PerfPoint iterations (default 1000000)\n";
            return 0;
        }
    }
    
    std::cout << "=== SpDiag PerfPoint Overhead Benchmark ===\n";
    std::cout << "  Iterations: " << iterations << "\n\n";
    
    std::cout << "Warming up...\n";
    PerfPoint pt(PerfKeyDefined::PROCESS_WORK, PerfLevel::KEY_MODULE);
    for (int i = 0; i < 100; ++i) {
        pt.Start();
        pt.End(0);
    }
    
    std::cout << "Running benchmark (N=" << iterations << ")...\n";
    
    auto startTotal = std::chrono::steady_clock::now();
    
    for (uint64_t i = 0; i < iterations; ++i) {
        pt.Start();
        pt.End(0);
    }
    
    auto endTotal = std::chrono::steady_clock::now();
    
    uint64_t totalDuration = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(endTotal - startTotal).count());
    
    double avgPerIteration = static_cast<double>(totalDuration) / iterations;
    
    std::cout << "\n=== Results ===\n";
    std::cout << "  Total Duration:     " << std::fixed << std::setprecision(3) 
              << (totalDuration / 1000000.0) << " ms\n";
    std::cout << "  Avg per iteration:  " << std::fixed << std::setprecision(2) 
              << avgPerIteration << " ns\n";
    
    std::cout << "\nConclusion:\n";
    std::cout << "  Single PerfPoint (constructor + End) overhead: " 
              << std::fixed << std::setprecision(2) 
              << avgPerIteration << " ns\n";
    
    return 0;
}
