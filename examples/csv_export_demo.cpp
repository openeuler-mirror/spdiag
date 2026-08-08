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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace {

volatile std::sig_atomic_t g_stopRequested = 0;
std::atomic<uint64_t> g_checksum{0};

void HandleSignal(int) {
    g_stopRequested = 1;
}

bool ParsePositiveInt(const char* value, int& result) {
    char* end = nullptr;
    long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed < 0 || parsed > 86400) {
        return false;
    }
    result = static_cast<int>(parsed);
    return true;
}

void RunWorkload(uint32_t workerId, int durationSec) {
    std::mt19937 rng(0xC5A000U + workerId);
    const auto deadline = durationSec > 0
        ? std::chrono::steady_clock::now() + std::chrono::seconds(durationSec)
        : std::chrono::steady_clock::time_point::max();

    while (g_stopRequested == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        {
            SpDiag::PerfPoint point(SpDiag::PerfKeyDefined::COMPUTE_SORT,
                                    SpDiag::PerfLevel::KEY_MODULE);
            point.Start();
            std::vector<uint32_t> values(4096);
            std::generate(values.begin(), values.end(), [&rng]() { return rng(); });
            std::sort(values.begin(), values.end());
            g_checksum.fetch_xor(values[workerId % values.size()],
                                 std::memory_order_relaxed);
            point.End();
        }

        {
            SpDiag::PerfPoint point(SpDiag::PerfKeyDefined::MEMORY_ALLOC,
                                    SpDiag::PerfLevel::DEBUG);
            point.Start();
            std::vector<uint64_t> buffer(64 * 1024, workerId + 1);
            uint64_t sum = 0;
            for (size_t i = 0; i < buffer.size(); i += 16) {
                buffer[i] += i;
                sum += buffer[i];
            }
            g_checksum.fetch_xor(sum, std::memory_order_relaxed);
            point.End();
        }

        {
            SpDiag::PerfPoint point(SpDiag::PerfKeyDefined::IO_IOTASK,
                                    SpDiag::PerfLevel::MODULE);
            point.Start();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            point.End();
        }
    }
}

void PrintUsage(const char* program) {
    std::cout << "Usage: " << program << " [--duration seconds] [--threads count]\n"
              << "  --duration, -d  Run time in seconds (0 waits for SIGINT/SIGTERM, default 60)\n"
              << "  --threads,  -t  Number of workload threads (default 3)\n";
}

}  // namespace

int main(int argc, char** argv) {
    int durationSec = 60;
    int threadCount = 3;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--duration" || arg == "-d") && i + 1 < argc) {
            if (!ParsePositiveInt(argv[++i], durationSec)) {
                std::cerr << "Invalid duration\n";
                return 2;
            }
        } else if ((arg == "--threads" || arg == "-t") && i + 1 < argc) {
            if (!ParsePositiveInt(argv[++i], threadCount) || threadCount < 1 || threadCount > 64) {
                std::cerr << "Invalid thread count\n";
                return 2;
            }
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            PrintUsage(argv[0]);
            return 2;
        }
    }

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    SpDiag::InitPerf("csv_export_demo");

    const auto pid = getpid();
    std::cout << "csv_export_demo PID=" << pid
              << " duration=" << durationSec
              << " threads=" << threadCount << "\n";
    std::cout.flush();

    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(threadCount));
    for (int i = 0; i < threadCount; ++i) {
        workers.emplace_back(RunWorkload, static_cast<uint32_t>(i), durationSec);
    }
    for (auto& worker : workers) {
        worker.join();
    }

    std::cout << "csv_export_demo finished checksum="
              << g_checksum.load(std::memory_order_relaxed) << "\n";
    return 0;
}
