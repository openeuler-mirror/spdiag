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

using namespace SpDiag;

static std::atomic<bool> running{true};

static void WorkerThread(int threadId, int iterations) {
    PerfKeyDefined key = static_cast<PerfKeyDefined>(
        (threadId % static_cast<int>(PerfKeyDefined::PERF_KEY_DEFINED_COUNT)) + 1);

    uint64_t count = 0;
    while (running.load(std::memory_order_relaxed) && count < static_cast<uint64_t>(iterations)) {
        PerfPoint pt(key, PerfLevel::KEY_MODULE);
        pt.Start();
        pt.End(0);
        ++count;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::cout << "[Thread " << threadId << "] " << count << " writes\n" << std::flush;
}

int main(int argc, char* argv[]) {
    std::string programName = "worker";
    int threads = 4;
    int durationSec = 30;
    int dumpIntervalMs = 0;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            programName = argv[++i];
        } else if (std::strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            threads = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            durationSec = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-di") == 0 && i + 1 < argc) {
            dumpIntervalMs = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            std::cout << "Usage: test_dump_conflict_worker [options]\n"
                      << "  -n <name>       Program name (unique per process)\n"
                      << "  -t <threads>    Worker threads (default 4)\n"
                      << "  -d <seconds>    Duration (default 30)\n"
                      << "  -di <ms>        Dump interval (default: from SHM or 1000)\n";
            return 0;
        }
    }

    std::cout << "=== Worker: " << programName << " ===\n"
              << "  Threads: " << threads << "\n"
              << "  Duration: " << durationSec << "s\n"
              << "  Dump interval: " << (dumpIntervalMs > 0 ? std::to_string(dumpIntervalMs) + "ms" : "default") << "\n" << std::flush;

    PerfConfig config;
    config.programName = programName;
    if (dumpIntervalMs > 0) {
        config.dumpIntervalMs = static_cast<uint32_t>(dumpIntervalMs);
    }
    PerfManager::Instance().Init(config);

    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (int i = 0; i < threads; ++i) {
        workers.emplace_back(WorkerThread, i, durationSec * 1000);
    }

    std::this_thread::sleep_for(std::chrono::seconds(durationSec));
    running.store(false, std::memory_order_relaxed);

    for (auto& w : workers) {
        w.join();
    }

    std::cout << "[" << programName << "] done\n" << std::flush;
    return 0;
}
