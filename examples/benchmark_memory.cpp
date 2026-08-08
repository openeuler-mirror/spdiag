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
#include "spdiag/shm_layout.h"

#include <iostream>
#include <vector>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <iomanip>
#include <thread>
#include <mutex>
#include <random>

#include <unistd.h>
#include <fstream>
#include <sstream>

using namespace SpDiag;

struct MemoryBenchmarkResult {
    std::string configName;
    uint32_t numCores;
    uint32_t maxPoints;
    uint32_t dumpIntervalMs;
    
    size_t baselineMemoryKB;
    size_t monitoredMemoryKB;
    size_t sdkMemoryIncrementKB;
    
    size_t shmTotalSize;
    size_t shmHeaderSize;
    size_t shmMetadataSize;
    size_t shmShardArraySize;
    size_t shmPerCoreSize;
    
    double memoryOverheadPercent;
};

static size_t GetProcessMemoryKB() {
    std::ifstream status("/proc/self/status");
    std::string line;
    
    while (std::getline(status, line)) {
        if (line.substr(0, 6) == "VmRSS:") {
            std::istringstream iss(line.substr(6));
            size_t value;
            iss >> value;
            return value;
        }
    }
    return 0;
}

static void CalculateShmLayout(uint32_t numCores, uint32_t maxPoints, 
                               size_t& totalSize, size_t& headerSize,
                               size_t& metadataSize, size_t& shardArraySize,
                               size_t& perCoreSize) {
    uint32_t effectiveMaxSlots = maxPoints;

    headerSize = sizeof(ShmHeader);

    size_t registrySize = sizeof(ProgramEntry) * maxProgramsDefault;
    size_t keyNamesSize = effectiveMaxSlots * nameLen;
    size_t keyModuleNamesSize = effectiveMaxSlots * nameLen;
    size_t activeBitmapSize = effectiveMaxSlots;
    metadataSize = registrySize + keyNamesSize + keyModuleNamesSize + activeBitmapSize;

    perCoreSize = ShmShardSize(effectiveMaxSlots);
    shardArraySize = perCoreSize * numCores;

    totalSize = headerSize + metadataSize + shardArraySize;
}

static size_t CalculateTheoreticalMemoryKB(uint32_t numCores, uint32_t maxPoints) {
    uint32_t effectiveMaxSlots = maxPoints;
    
    size_t headerSize = sizeof(ShmHeader);
    size_t registrySize = sizeof(ProgramEntry) * maxProgramsDefault;
    size_t keyNamesSize = effectiveMaxSlots * nameLen;
    size_t keyModuleNamesSize = effectiveMaxSlots * nameLen;
    size_t activeBitmapSize = effectiveMaxSlots;
    size_t metadataSize = registrySize + keyNamesSize + keyModuleNamesSize + activeBitmapSize;
    
    size_t perCoreSize = ShmShardSize(effectiveMaxSlots);
    size_t shardArraySize = perCoreSize * numCores;
    
    size_t totalSize = headerSize + metadataSize + shardArraySize;
    
    return totalSize / 1024;
}

static MemoryBenchmarkResult RunMemoryBenchmark(uint32_t numCores, uint32_t maxPoints, 
                                               uint32_t dumpIntervalMs,
                                               const std::string& configName) {
    std::cout << "Benchmarking: " << configName 
              << " (cores=" << numCores 
              << ", points=" << maxPoints 
              << ", dump=" << dumpIntervalMs << "ms)..." << std::flush;
    
    MemoryBenchmarkResult result;
    result.configName = configName;
    result.numCores = numCores;
    result.maxPoints = maxPoints;
    result.dumpIntervalMs = dumpIntervalMs;
    
    result.baselineMemoryKB = GetProcessMemoryKB();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    result.baselineMemoryKB = GetProcessMemoryKB();
    
    // Ensure previous PerfManager is shutdown before initializing new one
    PerfManager::Instance().Shutdown();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Generate unique SHM name with random suffix to avoid conflicts
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(1000, 9999);
    std::string shmName = "/spdiag_mem_bench_" + std::to_string(numCores) + "_" + 
                          std::to_string(maxPoints) + "_" + std::to_string(distrib(gen));
    
    PerfConfig config;
    config.shmName = shmName;
    config.mode = RunMode::DEBUG;
    config.levelGate = 4;
    config.dumpIntervalMs = dumpIntervalMs;
    
    try {
        PerfManager::Instance().Init(config);
        
        // Verify initialization succeeded
        if (!PerfManager::Instance().IsInitialized()) {
            std::cerr << " Failed to initialize PerfManager!\n";
            return result;
        }
    } catch (const std::exception& e) {
        std::cerr << " Exception during initialization: " << e.what() << "\n";
        return result;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    result.monitoredMemoryKB = GetProcessMemoryKB();
    result.sdkMemoryIncrementKB = result.monitoredMemoryKB - result.baselineMemoryKB;
    
    CalculateShmLayout(numCores, maxPoints, 
                      result.shmTotalSize, result.shmHeaderSize,
                      result.shmMetadataSize, result.shmShardArraySize,
                      result.shmPerCoreSize);
    
    result.shmTotalSize /= 1024;
    result.shmHeaderSize /= 1024;
    result.shmMetadataSize /= 1024;
    result.shmShardArraySize /= 1024;
    result.shmPerCoreSize /= 1024;
    
    result.memoryOverheadPercent = (static_cast<double>(result.sdkMemoryIncrementKB) / 
                                    result.baselineMemoryKB) * 100.0;
    
    size_t theoreticalMemoryKB = CalculateTheoreticalMemoryKB(numCores, maxPoints);
    
    std::cout << " Done! (Memory: " << result.sdkMemoryIncrementKB << " KB, Theoretical: " 
              << theoreticalMemoryKB << " KB";
    if (theoreticalMemoryKB > 0) {
        double diff = static_cast<double>(result.sdkMemoryIncrementKB - theoreticalMemoryKB) / theoreticalMemoryKB * 100.0;
        std::cout << ", Diff: " << std::fixed << std::setprecision(1) << diff << "%";
    }
    std::cout << ")\n";
    
    PerfManager::Instance().Shutdown();
    
    return result;
}

static void PrintMemoryResults(const std::vector<MemoryBenchmarkResult>& results) {
    std::cout << "\n";
    std::cout << "================================================================================\n";
    std::cout << "                       MEMORY OVERHEAD BENCHMARK RESULTS                        \n";
    std::cout << "================================================================================\n\n";
    
    std::cout << std::left << std::setw(25) << "Config"
              << std::setw(10) << "Cores"
              << std::setw(10) << "Points"
              << std::setw(10) << "Dump(ms)"
              << std::setw(15) << "SDK Inc(KB)"
              << std::setw(15) << "SHM Total(KB)"
              << std::setw(12) << "Overhead %"
              << "\n";
    
    std::cout << std::string(97, '-') << "\n";
    
    for (const auto& r : results) {
        std::cout << std::left << std::setw(25) << r.configName
                  << std::setw(10) << r.numCores
                  << std::setw(10) << r.maxPoints
                  << std::setw(10) << r.dumpIntervalMs
                  << std::setw(15) << r.sdkMemoryIncrementKB
                  << std::setw(15) << r.shmTotalSize
                  << std::setw(12) << std::to_string(r.memoryOverheadPercent).substr(0, 5) + "%"
                  << "\n";
    }
    
    std::cout << "\n";
    std::cout << "================================================================================\n";
    std::cout << "                          SHARED MEMORY LAYOUT ANALYSIS                         \n";
    std::cout << "================================================================================\n\n";
    
    const auto& r = results[0];
    std::cout << "Example Configuration: " << r.configName << "\n";
    std::cout << "  Cores:    " << r.numCores << "\n";
    std::cout << "  Points:   " << r.maxPoints << "\n";
    std::cout << "  Dump:     " << r.dumpIntervalMs << " ms\n\n";
    
    std::cout << "Shared Memory Breakdown:\n";
    std::cout << "  Header:      " << r.shmHeaderSize << " KB\n";
    std::cout << "  Metadata:    " << r.shmMetadataSize << " KB\n";
    std::cout << "  ShardArray:  " << r.shmShardArraySize << " KB\n";
    std::cout << "  Per-Core:    " << r.shmPerCoreSize << " KB/core\n";
    std::cout << "  ─────────────────────────────\n";
    std::cout << "  Total:       " << r.shmTotalSize << " KB\n\n";
    
    size_t theoreticalMemoryKB = CalculateTheoreticalMemoryKB(r.numCores, r.maxPoints);
    std::cout << "Theoretical Memory Overhead: " << theoreticalMemoryKB << " KB\n";
    std::cout << "Measured Memory Overhead:    " << r.sdkMemoryIncrementKB << " KB\n";
    if (theoreticalMemoryKB > 0) {
        double diff = static_cast<double>(r.sdkMemoryIncrementKB - theoreticalMemoryKB) / theoreticalMemoryKB * 100.0;
        std::cout << "Difference: " << std::fixed << std::setprecision(1) << diff << "%\n\n";
    } else {
        std::cout << "\n";
    }
    
    std::cout << "Memory per monitoring point:\n";
    size_t pointMemoryKB = r.shmShardArraySize / r.maxPoints;
    std::cout << "  " << pointMemoryKB << " KB/point (" 
              << (pointMemoryKB * 1024 / sizeof(PerfStat)) << " stats per point)\n\n";
    
    std::cout << "Scaling Analysis:\n";
    std::cout << "  Core scaling: " << r.shmPerCoreSize << " KB per additional core\n";
    std::cout << "  Point scaling: " << (r.shmShardArraySize / r.maxPoints) 
              << " KB per additional point\n\n";
    
    std::cout << "================================================================================\n";
    std::cout << "                                   SUMMARY                                      \n";
    std::cout << "================================================================================\n\n";
    
    size_t minMemory = SIZE_MAX;
    size_t maxMemory = 0;
    for (const auto& r : results) {
        if (r.sdkMemoryIncrementKB < minMemory) minMemory = r.sdkMemoryIncrementKB;
        if (r.sdkMemoryIncrementKB > maxMemory) maxMemory = r.sdkMemoryIncrementKB;
    }
    
    std::cout << "Memory Usage Range:\n";
    std::cout << "  Minimum: " << minMemory << " KB (" << (minMemory / 1024.0) << " MB)\n";
    std::cout << "  Maximum: " << maxMemory << " KB (" << (maxMemory / 1024.0) << " MB)\n";
    std::cout << "  Average: " << (minMemory + maxMemory) / 2 << " KB\n\n";
    
    std::cout << "PRODUCTION RECOMMENDATIONS:\n";
    std::cout << "  - For 4-core systems: Use maxPoints=500 (estimated " 
              << (results[0].shmPerCoreSize * 4 + results[0].shmHeaderSize + results[0].shmMetadataSize) 
              << " KB SHM)\n";
    std::cout << "  - For 8-core systems: Use maxPoints=1000 (estimated " 
              << (results[0].shmPerCoreSize * 8 + results[0].shmHeaderSize + results[0].shmMetadataSize) 
              << " KB SHM)\n";
    std::cout << "  - For 16+ core systems: Use maxPoints=2000 (monitor critical paths only)\n";
    std::cout << "  - Memory is pre-allocated: No runtime allocation overhead\n";
    std::cout << "  - SHM is shared across processes: Only one copy in system\n";
    std::cout << "\n";
}

int main(int argc, char* argv[]) {
    bool quickMode = false;
    
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-q") == 0) {
            quickMode = true;
        } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            std::cout << "Usage: benchmark_memory [-q] (quick mode)\n"
                      << "  -q  Quick mode (fewer configurations)\n"
                      << "\nThis benchmark measures the memory overhead of SpDiag monitoring.\n";
            return 0;
        }
    }
    
    std::cout << "=== SpDiag Memory Overhead Benchmark ===\n";
    std::cout << "Mode: " << (quickMode ? "Quick" : "Full") << "\n\n";
    
    std::vector<MemoryBenchmarkResult> results;
    
    if (quickMode) {
        results.push_back(RunMemoryBenchmark(1, 256, 500, "1core_256pts_500ms"));
        results.push_back(RunMemoryBenchmark(4, 500, 500, "4core_500pts_500ms"));
        results.push_back(RunMemoryBenchmark(8, 1000, 500, "8core_1000pts_500ms"));
        results.push_back(RunMemoryBenchmark(16, 2000, 1000, "16core_2000pts_1000ms"));
    } else {
        results.push_back(RunMemoryBenchmark(1, 256, 500, "1core_256pts_500ms"));
        results.push_back(RunMemoryBenchmark(1, 512, 500, "1core_512pts_500ms"));
        results.push_back(RunMemoryBenchmark(1, 1024, 1000, "1core_1024pts_1000ms"));
        
        results.push_back(RunMemoryBenchmark(4, 100, 100, "4core_100pts_100ms"));
        results.push_back(RunMemoryBenchmark(4, 500, 500, "4core_500pts_500ms"));
        results.push_back(RunMemoryBenchmark(4, 1000, 1000, "4core_1000pts_1000ms"));
        
        results.push_back(RunMemoryBenchmark(8, 500, 500, "8core_500pts_500ms"));
        results.push_back(RunMemoryBenchmark(8, 1000, 500, "8core_1000pts_500ms"));
        results.push_back(RunMemoryBenchmark(8, 2000, 1000, "8core_2000pts_1000ms"));
        
        results.push_back(RunMemoryBenchmark(16, 1000, 500, "16core_1000pts_500ms"));
        results.push_back(RunMemoryBenchmark(16, 2000, 1000, "16core_2000pts_1000ms"));
        
        results.push_back(RunMemoryBenchmark(32, 2000, 1000, "32core_2000pts_1000ms"));
        results.push_back(RunMemoryBenchmark(32, 5000, 1000, "32core_5000pts_1000ms"));
    }
    
    std::cout << "\n";
    PrintMemoryResults(results);
    
    return 0;
}
