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
#include "spdiag/perf_level.h"
#include "spdiag/perf_key_defined.h"
#include "spdiag/perf_config.h"
#include "spdiag/perf_manager.h"

#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <algorithm>
#include <sstream>

#include <sched.h>
#include <unistd.h>
#include <sys/sysinfo.h>

using namespace SpDiag;

static std::atomic<bool> running{true};
static std::atomic<uint64_t> totalWrites{0};
static std::atomic<uint64_t> totalReads{0};
static std::atomic<uint64_t> readRetries{0};
static std::mutex printMu;

static void SafePrint(const char* msg) {
    std::lock_guard<std::mutex> lock(printMu);
    std::cout << msg << std::flush;
}

static int GetCurrentCoreId() {
    return sched_getcpu();
}

static void SetThreadAffinity(int cpuId) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpuId, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

static void SetProcessAffinity(const std::vector<int>& cpus) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int cpu : cpus) {
        CPU_SET(cpu, &cpuset);
    }
    sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
}

static int GetNumCores() {
    return sysconf(_SC_NPROCESSORS_ONLN);
}

static std::vector<int> ParseCpuList(const std::string& cpuList) {
    std::vector<int> cpus;
    
    if (cpuList.empty()) {
        int numCores = GetNumCores();
        for (int i = 0; i < numCores; ++i) {
            cpus.push_back(i);
        }
        return cpus;
    }
    
    size_t dash = cpuList.find('-');
    if (dash != std::string::npos) {
        int start = std::stoi(cpuList.substr(0, dash));
        int end = std::stoi(cpuList.substr(dash + 1));
        for (int i = start; i <= end; ++i) {
            cpus.push_back(i);
        }
    } else {
        std::stringstream ss(cpuList);
        std::string token;
        while (std::getline(ss, token, ',')) {
            cpus.push_back(std::stoi(token));
        }
    }
    
    return cpus;
}

static void WriteThread_Bound(int threadId, int cpuId, int iterations, int pointId) {
    SetThreadAffinity(cpuId);
    
    PerfKeyDefined key = static_cast<PerfKeyDefined>(pointId);
    PerfPoint pt(key, PerfLevel::KEY_MODULE);
    
    uint64_t localWrites = 0;
    while (running.load(std::memory_order_relaxed) && localWrites < static_cast<uint64_t>(iterations)) {
        pt.Start();
        pt.End(0);
        localWrites++;
    }
    
    totalWrites.fetch_add(localWrites);
    
    char buf[256];
    std::snprintf(buf, sizeof(buf), 
        "[WriteThread %d] CPU %d: completed %lu writes (point %d)\n",
        threadId, cpuId, localWrites, pointId);
    SafePrint(buf);
}

static void WriteThread_Unbound(int threadId, int iterations, int pointId) {
    PerfKeyDefined key = static_cast<PerfKeyDefined>(pointId);
    PerfPoint pt(key, PerfLevel::KEY_MODULE);
    
    uint64_t localWrites = 0;
    int lastCpu = -1;
    int migrations = 0;
    
    while (running.load(std::memory_order_relaxed) && localWrites < static_cast<uint64_t>(iterations)) {
        int currentCpu = GetCurrentCoreId();
        if (lastCpu != -1 && currentCpu != lastCpu) {
            migrations++;
        }
        lastCpu = currentCpu;
        
        pt.Start();
        pt.End(0);
        localWrites++;
    }
    
    totalWrites.fetch_add(localWrites);
    
    char buf[256];
    std::snprintf(buf, sizeof(buf), 
        "[WriteThread %d] completed %lu writes, %d migrations (point %d)\n",
        threadId, localWrites, migrations, pointId);
    SafePrint(buf);
}

static void ReadThread_Bound(int threadId, int cpuId, int readIntervalMs, int durationSec) {
    SetThreadAffinity(cpuId);
    
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(durationSec);
    uint64_t localReads = 0;
    uint64_t localRetries = 0;
    
    while (std::chrono::steady_clock::now() < deadline && running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(readIntervalMs));
        localReads++;
    }
    
    totalReads.fetch_add(localReads);
    readRetries.fetch_add(localRetries);
    
    char buf[256];
    std::snprintf(buf, sizeof(buf), 
        "[ReadThread %d] CPU %d: completed %lu reads, %lu retries\n",
        threadId, cpuId, localReads, localRetries);
    SafePrint(buf);
}

static void ReadThread_Unbound(int threadId, int readIntervalMs, int durationSec) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(durationSec);
    uint64_t localReads = 0;
    uint64_t localRetries = 0;
    int lastCpu = -1;
    int migrations = 0;
    
    while (std::chrono::steady_clock::now() < deadline && running.load(std::memory_order_relaxed)) {
        int currentCpu = GetCurrentCoreId();
        if (lastCpu != -1 && currentCpu != lastCpu) {
            migrations++;
        }
        lastCpu = currentCpu;
        
        std::this_thread::sleep_for(std::chrono::milliseconds(readIntervalMs));
        localReads++;
    }
    
    totalReads.fetch_add(localReads);
    readRetries.fetch_add(localRetries);
    
    char buf[256];
    std::snprintf(buf, sizeof(buf), 
        "[ReadThread %d] completed %lu reads, %lu retries, %d migrations\n",
        threadId, localReads, localRetries, migrations);
    SafePrint(buf);
}

int main(int argc, char* argv[]) {
    int writeThreads = 4;
    int readThreads = 1;
    int iterations = 1000000;
    int durationSec = 10;
    int readIntervalMs = 100;
    std::string cpuList;
    bool bindMode = true;
    
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
            writeThreads = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            readThreads = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            iterations = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            durationSec = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-ri") == 0 && i + 1 < argc) {
            readIntervalMs = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            cpuList = argv[++i];
        } else if (std::strcmp(argv[i], "-b") == 0) {
            bindMode = true;
        } else if (std::strcmp(argv[i], "-nb") == 0) {
            bindMode = false;
        } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            std::cout << "Usage: test_conflict [options]\n"
                      << "  -w <num>     Write threads (default 4)\n"
                      << "  -r <num>     Read threads (default 1)\n"
                      << "  -i <num>     Iterations per write thread (default 1000000)\n"
                      << "  -d <sec>     Duration in seconds (default 10)\n"
                      << "  -ri <ms>     Read interval in ms (default 100)\n"
                      << "  -c <list>    CPU list (e.g., \"0-7\" or \"0,2,4,6\")\n"
                      << "  -b           Bind mode: each thread bound to specific CPU\n"
                      << "  -nb          No-bind mode: threads can migrate within CPU list\n"
                      << "\nExamples:\n"
                      << "  # Bind mode: 8 threads on CPU 0-7, each thread bound to one CPU\n"
                      << "  test_conflict -w 8 -c 0-7 -b\n"
                      << "\n  # No-bind mode: 8 threads on CPU 0-7, threads can migrate\n"
                      << "  test_conflict -w 8 -c 0-7 -nb\n"
                      << "\n  # Mixed: 100 write threads, 5 read threads on CPU 0-99\n"
                      << "  test_conflict -w 100 -r 5 -c 0-99 -b\n";
            return 0;
        }
    }
    
    std::vector<int> cpus = ParseCpuList(cpuList);
    
    SetProcessAffinity(cpus);
    
    std::cout << "=== Conflict Test ===\n";
    std::cout << "  Mode: " << (bindMode ? "Bind (each thread bound to specific CPU)" : "No-bind (threads can migrate)") << "\n";
    std::cout << "  Write threads: " << writeThreads << "\n";
    std::cout << "  Read threads: " << readThreads << "\n";
    std::cout << "  Iterations per write thread: " << iterations << "\n";
    std::cout << "  Duration: " << durationSec << " seconds\n";
    std::cout << "  Read interval: " << readIntervalMs << " ms\n";
    std::cout << "  CPU list: ";
    for (size_t i = 0; i < cpus.size(); ++i) {
        std::cout << cpus[i] << (i < cpus.size() - 1 ? "," : "");
    }
    std::cout << " (total " << cpus.size() << " cores)\n";
    std::cout << "  System cores: " << GetNumCores() << "\n\n";
    
    PerfConfig config;
    config.programName = "test_conflict";
    PerfManager::Instance().Init(config);
    
    std::vector<std::thread> threads;
    
    for (int i = 0; i < writeThreads; ++i) {
        int cpuId = cpus[i % cpus.size()];
        int pointId = i % static_cast<int>(PerfKeyDefined::PERF_KEY_DEFINED_COUNT);
        
        if (bindMode) {
            threads.emplace_back(WriteThread_Bound, i, cpuId, iterations, pointId);
        } else {
            threads.emplace_back(WriteThread_Unbound, i, iterations, pointId);
        }
    }
    
    for (int i = 0; i < readThreads; ++i) {
        int cpuId = cpus[(writeThreads + i) % cpus.size()];
        
        if (bindMode) {
            threads.emplace_back(ReadThread_Bound, i, cpuId, readIntervalMs, durationSec);
        } else {
            threads.emplace_back(ReadThread_Unbound, i, readIntervalMs, durationSec);
        }
    }
    
    std::cout << "Waiting for all threads to complete...\n\n";
    
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "\n=== Test Results ===\n";
    std::cout << "  Total writes: " << totalWrites.load() << "\n";
    std::cout << "  Total reads: " << totalReads.load() << "\n";
    std::cout << "  Read retries: " << readRetries.load() << "\n";
    
    return 0;
}
