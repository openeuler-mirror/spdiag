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

enum class TaskType {
    MATMUL = 0,
    PRIME,
    IO,
    PROCESS,
    PIPELINE,
    LOWLEVEL,
    ALLOC,
    COUNT
};

static std::atomic<bool> running{true};
static std::mutex printMu;

static void SafePrint(const char* msg) {
    std::lock_guard<std::mutex> lock(printMu);
    std::cout << msg << std::flush;
}

static int GetCurrentCoreId() {
    return sched_getcpu();
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

static void ThreadMatMul(int globalThreadId, int localThreadId, int iterations) {
    srand(static_cast<unsigned>(globalThreadId * 12345 + 67890));
    int iter = 0;
    PerfPoint pt(PerfKeyDefined::COMPUTE_MATMUL, PerfLevel::KEY_MODULE);
    while (running.load(std::memory_order_relaxed) && iter < iterations) {
        int r = rand() % 10;
        if (r == 0) {
            pt.Start();
            MatrixMultiply(32);
        } else if (r == 1) {
            pt.Start();
            MatrixMultiply(32);
            pt.End(1);
        } else {
            pt.Start();
            MatrixMultiply(32);
            pt.End(0);
        }
        ++iter;
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf), "[TaskGroup %d Thread %d (local %d) MatMul] completed %d iterations (on CPU %d)\n",
                  globalThreadId / 8, globalThreadId, localThreadId, iter, GetCurrentCoreId());
    SafePrint(buf);
}

static void ThreadPrime(int globalThreadId, int localThreadId, int iterations) {
    int iter = 0;
    PerfPoint pt(PerfKeyDefined::COMPUTE_PRIME, PerfLevel::KEY_MODULE);
    while (running.load(std::memory_order_relaxed) && iter < iterations) {
        pt.Start();
        CountPrimes(100000, 100500);
        pt.End(0);
        ++iter;
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf), "[TaskGroup %d Thread %d (local %d) Prime] completed %d iterations (on CPU %d)\n",
                  globalThreadId / 8, globalThreadId, localThreadId, iter, GetCurrentCoreId());
    SafePrint(buf);
}

static void ThreadIO(int globalThreadId, int localThreadId, int iterations) {
    int iter = 0;
    PerfPoint pt(PerfKeyDefined::IO_IOTASK, PerfLevel::KEY_MODULE);
    while (running.load(std::memory_order_relaxed) && iter < iterations) {
        pt.Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        pt.End(0);
        ++iter;
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf), "[TaskGroup %d Thread %d (local %d) IO] completed %d iterations (on CPU %d)\n",
                  globalThreadId / 8, globalThreadId, localThreadId, iter, GetCurrentCoreId());
    SafePrint(buf);
}

static void ThreadProcess(int globalThreadId, int localThreadId, int iterations) {
    int iter = 0;
    PerfPoint pt(PerfKeyDefined::PROCESS_WORK, PerfLevel::KEY_MODULE);
    while (running.load(std::memory_order_relaxed) && iter < iterations) {
        pt.Start();
        auto t0 = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(1)) {
        }
        pt.End(0);
        ++iter;
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf), "[TaskGroup %d Thread %d (local %d) Process] completed %d iterations (on CPU %d)\n",
                  globalThreadId / 8, globalThreadId, localThreadId, iter, GetCurrentCoreId());
    SafePrint(buf);
}

static void ThreadPipeline(int globalThreadId, int localThreadId, int iterations) {
    int iter = 0;
    while (running.load(std::memory_order_relaxed) && iter < iterations) {
        PerfPoint pt1(PerfKeyDefined::PIPELINE_STAGE1, PerfLevel::KEY_MODULE);
        PerfPoint pt2(PerfKeyDefined::PIPELINE_STAGE2, PerfLevel::KEY_MODULE);
        PerfPoint pt3(PerfKeyDefined::PIPELINE_STAGE3, PerfLevel::KEY_MODULE);
        {
            pt1.Start();
            SortArray(256);
            pt1.End(0);
        }
        {
            pt2.Start();
            SearchArray(256);
            pt2.End(0);
        }
        {
            pt3.Start();
            SumArray(256);
            pt3.End(0);
        }
        ++iter;
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf), "[TaskGroup %d Thread %d (local %d) Pipeline] completed %d iterations (on CPU %d)\n",
                  globalThreadId / 8, globalThreadId, localThreadId, iter, GetCurrentCoreId());
    SafePrint(buf);
}

static void ThreadLowLevel(int globalThreadId, int localThreadId, int iterations) {
    int iter = 0;
    PerfPoint pt(PerfKeyDefined::SYSTEM_SCHEDULER, PerfLevel::SUB_SYSTEM);
    while (running.load(std::memory_order_relaxed) && iter < iterations) {
        pt.Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        pt.End(0);
        ++iter;
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf), "[TaskGroup %d Thread %d (local %d) Scheduler] completed %d iterations (on CPU %d)\n",
                  globalThreadId / 8, globalThreadId, localThreadId, iter, GetCurrentCoreId());
    SafePrint(buf);
}

static void ThreadAlloc(int globalThreadId, int localThreadId, int iterations) {
    int iter = 0;
    PerfPoint pt(PerfKeyDefined::MEMORY_ALLOC, PerfLevel::DEBUG);
    while (running.load(std::memory_order_relaxed) && iter < iterations) {
        pt.Start();
        AllocDealloc();
        pt.End(0);
        ++iter;
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf), "[TaskGroup %d Thread %d (local %d) Alloc] completed %d iterations (on CPU %d)\n",
                  globalThreadId / 8, globalThreadId, localThreadId, iter, GetCurrentCoreId());
    SafePrint(buf);
}

static void WorkerFunction(int taskType, int globalThreadId, int localThreadId, int iterations) {
    switch (taskType) {
        case 0: ThreadMatMul(globalThreadId, localThreadId, iterations); break;
        case 1: ThreadPrime(globalThreadId, localThreadId, iterations); break;
        case 2: ThreadIO(globalThreadId, localThreadId, iterations); break;
        case 3: ThreadProcess(globalThreadId, localThreadId, iterations); break;
        case 4: ThreadPipeline(globalThreadId, localThreadId, iterations); break;
        case 5: ThreadLowLevel(globalThreadId, localThreadId, iterations); break;
        case 6: ThreadAlloc(globalThreadId, localThreadId, iterations); break;
    }
}

int main(int argc, char* argv[]) {
    int iterationsPerThread = 10000;
    int baseThreads = 8;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            iterationsPerThread = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            baseThreads = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            std::cout << "Usage: demo_app_concurrent [-i iterations] [-b base_threads]\n"
                      << "  -i  Iterations per thread (default 10000)\n"
                      << "  -b  Base number of threads per task group (default 8)\n"
                      << "\nTask groups: 8 groups with thread counts: base, base+1, ..., base+7\n"
                      << "  Example: base=8 -> groups have 8,9,10,11,12,13,14,15 threads\n"
                      << "\nNOTE: This is an UNBOUND version - threads are NOT pinned to CPU cores.\n"
                      << "      OS scheduler is free to migrate threads across cores.\n"
                      << "\nAfter execution, use 'spdiag show' to view performance statistics.\n";
            return 0;
        }
    }

    int totalThreads = 0;
    for (int i = 0; i < static_cast<int>(TaskType::COUNT); ++i) {
        totalThreads += baseThreads + i;
    }

    std::cout << "=== SpDiag Concurrent Application (UNBOUND VERSION) ===\n";
    std::cout << "  Iterations per thread: " << iterationsPerThread << "\n";
    std::cout << "  Task Groups: " << static_cast<int>(TaskType::COUNT) << "\n";
    std::cout << "  Thread counts per group: ";
    for (int i = 0; i < static_cast<int>(TaskType::COUNT); ++i) {
        std::cout << (baseThreads + i) << (i < static_cast<int>(TaskType::COUNT) - 1 ? "," : "");
    }
    std::cout << "\n";
    std::cout << "  Total threads: " << totalThreads << "\n";
    std::cout << "  Total expected iterations: " << (totalThreads * iterationsPerThread) << "\n";
    std::cout << "  CPU Affinity: NOT SET (OS can migrate threads)\n\n";

    std::vector<std::thread> workers;
    workers.reserve(totalThreads);

    int globalThreadId = 0;
    for (int taskIdx = 0; taskIdx < static_cast<int>(TaskType::COUNT); ++taskIdx) {
        int numThreadsInGroup = baseThreads + taskIdx;

        std::cout << "Starting Task Group " << (taskIdx + 1) << ": "
                  << numThreadsInGroup << " threads executing Task Type " << taskIdx << "...\n";

        for (int localThreadId = 0; localThreadId < numThreadsInGroup; ++localThreadId) {
            workers.emplace_back(WorkerFunction, taskIdx, globalThreadId, localThreadId, iterationsPerThread);
            ++globalThreadId;
        }
    }

    std::cout << "\nWaiting for all threads to complete...\n\n";

    for (auto& w : workers) {
        w.join();
    }

    std::cout << "\nAll workers finished.\n";
    std::cout << "Total threads: " << totalThreads << "\n";
    std::cout << "Total expected iterations: " << (totalThreads * iterationsPerThread) << "\n";
    std::cout << "Performance data has been recorded. Use 'spdiag show' to view statistics.\n";

    return 0;
}
