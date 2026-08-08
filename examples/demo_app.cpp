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

#include "spdiag/perf.h"

#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <algorithm>

#include <sched.h>
#include <unistd.h>

using namespace SpDiag;

static std::atomic<bool> running{true};
static std::mutex printMu;

static void SafePrint(const char* msg) {
    std::lock_guard<std::mutex> lock(printMu);
    std::cout << msg << std::flush;
}

static void SetAffinity(uint32_t core) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    sched_setaffinity(0, sizeof(cpuset), &cpuset);
}

static void MatrixMultiply(int size) {
    std::vector<double> a(size * size, 1.5);
    std::vector<double> b(size * size, 2.5);
    std::vector<double> c(size * size, 0.0);

    for (int i = 0; i < size; ++i) {
        for (int k = 0; k < size; ++k) {
            double aik = a[i * size + k];
            for (int j = 0; j < size; ++j) {
                c[i * size + j] += aik * b[k * size + j];
            }
        }
    }
}

static bool IsPrime(uint64_t n) {
    if (n < 2) return false;
    if (n < 4) return true;
    if (n % 2 == 0 || n % 3 == 0) return false;
    for (uint64_t i = 5; i * i <= n; i += 6) {
        if (n % i == 0 || n % (i + 2) == 0) return false;
    }
    return true;
}

static uint64_t CountPrimes(uint64_t from, uint64_t to) {
    uint64_t count = 0;
    for (uint64_t i = from; i <= to; ++i) {
        if (IsPrime(i)) ++count;
    }
    return count;
}

static void SortArray(int n) {
    std::vector<int> arr(n);
    for (int i = 0; i < n; ++i) arr[i] = rand();
    std::sort(arr.begin(), arr.end());
}

static void SearchArray(int n) {
    std::vector<int> arr(n);
    for (int i = 0; i < n; ++i) arr[i] = i * 3;
    int target = rand() % (n * 3);
    volatile bool found = std::binary_search(arr.begin(), arr.end(), target);
    (void)found;
}

static double SumArray(int n) {
    std::vector<double> arr(n);
    for (int i = 0; i < n; ++i) arr[i] = static_cast<double>(rand()) / RAND_MAX;
    double sum = 0.0;
    for (int i = 0; i < n; ++i) sum += arr[i];
    return sum;
}

static void AllocDealloc() {
    std::vector<double> buf(1024);
    for (int i = 0; i < 1024; ++i) buf[i] = static_cast<double>(i);
}

static void ThreadCacheStress(uint32_t core, int durationSec) {
    SetAffinity(core);
    const size_t ARRAY_SIZE = 16 * 1024 * 1024;
    std::vector<uint64_t> data(ARRAY_SIZE);
    for (size_t i = 0; i < ARRAY_SIZE; ++i) {
        data[i] = i;
    }
    for (size_t i = ARRAY_SIZE - 1; i > 0; --i) {
        size_t j = static_cast<size_t>(rand()) % (i + 1);
        std::swap(data[i], data[j]);
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(durationSec);
    uint64_t sum = 0;
    int iter = 0;
    while (running.load(std::memory_order_relaxed)) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        size_t idx = static_cast<size_t>(rand()) % ARRAY_SIZE;
        sum += data[idx];
        data[idx] = sum;
        ++iter;
    }
    volatile uint64_t sink = sum;
    (void)sink;
    char buf[128];
    std::snprintf(buf, sizeof(buf), "[Core %u CacheStress] completed %d iterations\n", core, iter);
    SafePrint(buf);
}

static void ThreadMatMul(uint32_t core, int durationSec) {
    SetAffinity(core);
    srand(static_cast<unsigned>(core * 12345 + 67890));
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(durationSec);
    int iter = 0;
    while (running.load(std::memory_order_relaxed)) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        int r = rand() % 10;
        if (r == 0) {
            PerfPoint pt(PerfKeyDefined::COMPUTE_MATMUL, PerfLevel::KEY_MODULE);
            pt.Start();
            MatrixMultiply(32);
        } else if (r == 1) {
            PerfPoint pt(PerfKeyDefined::COMPUTE_MATMUL, PerfLevel::KEY_MODULE);
            pt.Start();
            MatrixMultiply(32);
            pt.End(1);
        } else {
            PerfPoint pt(PerfKeyDefined::COMPUTE_MATMUL, PerfLevel::KEY_MODULE);
            pt.Start();
            MatrixMultiply(32);
            pt.End(0);
        }
        ++iter;
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf), "[Core %u MatMul] completed %d iterations\n", core, iter);
    SafePrint(buf);
}

static void ThreadPrime(uint32_t core, int durationSec) {
    SetAffinity(core);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(durationSec);
    int iter = 0;
    PerfPoint pt(PerfKeyDefined::COMPUTE_PRIME, PerfLevel::KEY_MODULE);
    while (running.load(std::memory_order_relaxed)) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        pt.Start();
        CountPrimes(100000, 100500);
        pt.End(0);
        ++iter;
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf), "[Core %u Prime] completed %d iterations\n", core, iter);
    SafePrint(buf);
}

static void ThreadIO(uint32_t core, int durationSec) {
    SetAffinity(core);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(durationSec);
    int iter = 0;
    PerfPoint pt(PerfKeyDefined::IO_IOTASK, PerfLevel::KEY_MODULE);
    while (running.load(std::memory_order_relaxed)) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        pt.Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        pt.End(0);
        ++iter;
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf), "[Core %u IO] completed %d iterations\n", core, iter);
    SafePrint(buf);
}

static void ThreadProcess(uint32_t core, int durationSec) {
    SetAffinity(core);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(durationSec);
    int iter = 0;
    PerfPoint pt(PerfKeyDefined::PROCESS_WORK, PerfLevel::KEY_MODULE);
    while (running.load(std::memory_order_relaxed)) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        pt.Start();
        auto t0 = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(1)) {
        }
        pt.End(0);
        ++iter;
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf), "[Core %u Process] completed %d iterations\n", core, iter);
    SafePrint(buf);
}

static void ThreadPipeline(uint32_t core, int durationSec) {
    SetAffinity(core);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(durationSec);
    int iter = 0;
    while (running.load(std::memory_order_relaxed)) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        {
            PerfPoint pt(PerfKeyDefined::PIPELINE_STAGE1, PerfLevel::KEY_MODULE);
            pt.Start();
            SortArray(256);
            pt.End(0);
        }
        {
            PerfPoint pt(PerfKeyDefined::PIPELINE_STAGE2, PerfLevel::KEY_MODULE);
            pt.Start();
            SearchArray(256);
            pt.End(0);
        }
        {
            PerfPoint pt(PerfKeyDefined::PIPELINE_STAGE3, PerfLevel::KEY_MODULE);
            pt.Start();
            SumArray(256);
            pt.End(0);
        }
        ++iter;
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf), "[Core %u Pipeline] completed %d iterations\n", core, iter);
    SafePrint(buf);
}

static void ThreadLowLevel(uint32_t core, int durationSec) {
    SetAffinity(core);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(durationSec);
    int iter = 0;
    PerfPoint pt(PerfKeyDefined::SYSTEM_SCHEDULER, PerfLevel::SUB_SYSTEM);
    while (running.load(std::memory_order_relaxed)) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        pt.Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        pt.End(0);
        ++iter;
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf), "[Core %u Scheduler] completed %d iterations\n", core, iter);
    SafePrint(buf);
}

static void ThreadAlloc(uint32_t core, int durationSec) {
    SetAffinity(core);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(durationSec);
    int iter = 0;
    PerfPoint pt(PerfKeyDefined::MEMORY_ALLOC, PerfLevel::DEBUG);
    while (running.load(std::memory_order_relaxed)) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        pt.Start();
        AllocDealloc();
        pt.End(0);
        ++iter;
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf), "[Core %u Alloc] completed %d iterations\n", core, iter);
    SafePrint(buf);
}

int main(int argc, char* argv[]) {
    int durationSec = 5;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            durationSec = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            std::cout << "Usage: demo_app [-d duration_sec]\n"
                      << "  -d  Duration in seconds (0 = infinite, default 5)\n";
            return 0;
        }
    }

    std::cout << "=== SpDiag Demo Application ===\n";
    std::cout << "  Duration: " << (durationSec > 0 ? std::to_string(durationSec) + "s" : "infinite") << "\n";
    std::cout << "  PID: " << getpid() << "\n\n";

    InitPerf("demo_app");

    std::vector<std::thread> workers;
    workers.reserve(9);
    workers.emplace_back(ThreadMatMul, 0, durationSec > 0 ? durationSec : 86400);
    workers.emplace_back(ThreadPrime,  1, durationSec > 0 ? durationSec : 86400);
    workers.emplace_back(ThreadPrime,  2, durationSec > 0 ? durationSec : 86400);
    workers.emplace_back(ThreadIO,     3, durationSec > 0 ? durationSec : 86400);
    workers.emplace_back(ThreadProcess, 4, durationSec > 0 ? durationSec : 86400);
    workers.emplace_back(ThreadPipeline,  5, durationSec > 0 ? durationSec : 86400);
    workers.emplace_back(ThreadLowLevel,  6, durationSec > 0 ? durationSec : 86400);
    workers.emplace_back(ThreadAlloc,     0, durationSec > 0 ? durationSec : 86400);
    workers.emplace_back(ThreadCacheStress, 7, durationSec > 0 ? durationSec : 86400);

    if (durationSec == 0) {
        std::cout << "Running indefinitely. Press Enter to stop...\n";
        std::cin.get();
        running.store(false, std::memory_order_relaxed);
    }

    for (auto& w : workers) {
        w.join();
    }

    std::cout << "\nAll workers finished.\n";

    return 0;
}
