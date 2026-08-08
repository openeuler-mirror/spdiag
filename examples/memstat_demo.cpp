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

#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <limits>

#include <unistd.h>
#include <sys/resource.h>
#include <malloc.h>
#include <csignal>

namespace {

volatile std::sig_atomic_t gStartSignal = 0;

void OnSigUsr1(int) {
    gStartSignal = 1;
}

size_t GetPeakRss() {
    struct rusage usage = {};
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss;
}

size_t GetHeapPeak() {
    struct mallinfo2 info = mallinfo2();
    return info.uordblks;
}

void BindToCore(int coreId) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(coreId, &cpuset);
    sched_setaffinity(0, sizeof(cpuset), &cpuset);
}

struct Config {
    std::string mode;
    int count = 200;
    int repeat = 1;
    int core = 0;
    bool wait = false;
    std::string csvPath;
};

void PrintUsage() {
    std::cerr <<
        "Usage: memstat_demo [options]\n"
        "\n"
        "Memory allocation verification demo for spdiag memstat.\n"
        "Without --mode, all workloads run in sequence (leak runs last).\n"
        "\n"
        "Modes:\n"
        "  normal    Fully paired alloc/free, uniform 1024 B\n"
        "  leak      50% leaked via simulated error path, 2048 B each\n"
        "  bimodal   10000x 64 B + 1x 64 MiB\n"
        "  churn     100000x alloc/free 256 B\n"
        "  burst     sleep -> alloc 100x 4 KB -> sleep -> free\n"
        "\n"
        "Options:\n"
        "  --mode <mode>     Run single mode (default: all)\n"
        "  --count <N>       Alloc count for normal/leak (default: 200)\n"
        "  --repeat <N>      Repeat count (default: 1)\n"
        "  --core <C>        CPU core to bind (default: 0)\n"
        "  --wait            Wait for ENTER before starting\n"
        "  --csv <path>      Output CSV file\n"
        "  --help, -h        Show this help\n";
}

Config ParseArgs(int argc, char* argv[]) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage();
            std::exit(0);
        } else if (arg == "--mode" && i + 1 < argc) {
            cfg.mode = argv[++i];
        } else if (arg == "--count" && i + 1 < argc) {
            cfg.count = std::atoi(argv[++i]);
        } else if (arg == "--repeat" && i + 1 < argc) {
            cfg.repeat = std::atoi(argv[++i]);
        } else if (arg == "--core" && i + 1 < argc) {
            cfg.core = std::atoi(argv[++i]);
        } else if (arg == "--wait") {
            cfg.wait = true;
        } else if (arg == "--csv" && i + 1 < argc) {
            cfg.csvPath = argv[++i];
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            PrintUsage();
            std::exit(1);
        }
    }
    return cfg;
}

volatile size_t gSink = 0;

} // namespace

static void WorkloadNormal(int count) {
    for (int i = 0; i < count; ++i) {
        char* p = new char[1024];
        std::memset(p, static_cast<unsigned char>(i & 0xFF), 1024);
        gSink += static_cast<size_t>(p[0]);
        delete[] p;
    }
}

static char** gLeakedPtrs = nullptr;
static int gLeakedCount = 0;

static void WorkloadLeak(int count) {
    int leakCount = (count + 1) / 2;
    gLeakedPtrs = static_cast<char**>(malloc(sizeof(char*) * static_cast<size_t>(leakCount)));
    gLeakedCount = 0;
    for (int i = 0; i < count; ++i) {
        char* p = new char[2048];
        std::memset(p, static_cast<unsigned char>(i & 0xFF), 2048);
        gSink += static_cast<size_t>(p[0]);
        if (i % 2 == 0) {
            delete[] p;
        } else {
            gLeakedPtrs[gLeakedCount++] = p;
        }
    }
}

static void WorkloadBimodal() {
    char** small = static_cast<char**>(malloc(sizeof(char*) * 10000));
    for (int i = 0; i < 10000; ++i) {
        small[i] = new char[64];
        gSink += static_cast<size_t>(small[i][0]);
    }
    auto huge = new char[64 * 1024 * 1024];
    gSink += static_cast<size_t>(huge[0]);
    for (int i = 0; i < 10000; ++i) delete[] small[i];
    free(small);
    delete[] huge;
}

static void WorkloadChurn() {
    for (int i = 0; i < 100000; ++i) {
        auto p = new char[256];
        gSink += static_cast<size_t>(p[0]);
        delete[] p;
    }
}

static void WorkloadBurst() {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    char** burst = static_cast<char**>(malloc(sizeof(char*) * 100));
    for (int i = 0; i < 100; ++i) {
        burst[i] = new char[4096];
        gSink += static_cast<size_t>(burst[i][0]);
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
    for (int i = 0; i < 100; ++i) delete[] burst[i];
    free(burst);
}

int main(int argc, char* argv[]) {
    Config cfg = ParseArgs(argc, argv);

    if (!cfg.mode.empty() && cfg.mode != "normal" && cfg.mode != "leak"
        && cfg.mode != "bimodal" && cfg.mode != "churn" && cfg.mode != "burst") {
        std::cerr << "Unknown mode: " << cfg.mode
                  << " (available: normal, leak, bimodal, churn, burst)\n";
        return 1;
    }

    std::cerr << "memstat_demo  PID=" << getpid() << std::endl;
    std::cerr << "  mode=" << (cfg.mode.empty() ? "all" : cfg.mode)
              << "  count=" << cfg.count
              << "  repeat=" << cfg.repeat
              << "  core=" << cfg.core << std::endl;

    if (cfg.wait) {
        std::signal(SIGUSR1, OnSigUsr1);
        std::cerr << "  Press ENTER or send SIGUSR1 to start..." << std::endl;

        while (gStartSignal == 0) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(STDIN_FILENO, &fds);
            struct timeval tv = {0, 100000};
            if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0) {
                char tmp;
                while (read(STDIN_FILENO, &tmp, 1) == 1 && tmp != '\n') {
                }
                break;
            }
        }
    }

    BindToCore(cfg.core);

    bool ranLeak = false;

    for (int r = 0; r < cfg.repeat; ++r) {
        if (cfg.mode.empty() || cfg.mode == "normal") {
            std::cerr << "  [normal] " << cfg.count << "x 1024 B alloc+free" << std::endl;
            WorkloadNormal(cfg.count);
        }
        if (cfg.mode.empty() || cfg.mode == "bimodal") {
            std::cerr << "  [bimodal] 10000x 64B + 1x 64 MiB" << std::endl;
            WorkloadBimodal();
        }
        if (cfg.mode.empty() || cfg.mode == "churn") {
            std::cerr << "  [churn] 100000x alloc/free 256B" << std::endl;
            WorkloadChurn();
        }
        if (cfg.mode.empty() || cfg.mode == "burst") {
            std::cerr << "  [burst] sleep 2s, alloc 100x 4KB, sleep 1s" << std::endl;
            WorkloadBurst();
        }
        if (cfg.mode.empty() || cfg.mode == "leak") {
            if (cfg.mode.empty()) {
                std::cerr << "\n  === All non-leak workloads finished ===\n";
                std::cerr << "  Leak workload will start in 2 seconds.\n";
                std::cerr << "  Start memstat NOW in another terminal:\n";
                std::cerr << "    sudo spdiag memstat --pid " << getpid() << " --human\n";
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
            std::cerr << "  [leak] " << cfg.count << "x 2048 B, 50% leaked" << std::endl;
            WorkloadLeak(cfg.count);
            ranLeak = true;
        }
    }

    if (ranLeak) {
        std::cerr << "\n  Leaked memory active (" << gLeakedCount * 2048 / 1024
                  << " KB). Process stays alive for observation.\n";
        std::cerr << "  Run: sudo spdiag memstat --pid " << getpid() << " --human\n";
        std::cerr << "  Press ENTER to exit.\n";
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cin.get();
        return 0;
    }

    size_t rssPeak = GetPeakRss();
    size_t heapPeak = GetHeapPeak();

    std::cout << "\nmemstat_demo completed.\n"
              << "  RSS peak:  " << (rssPeak / 1024) << " MB\n"
              << "  Heap peak: " << (heapPeak / 1024 / 1024) << " MB\n"
              << std::endl;

    if (!cfg.csvPath.empty()) {
        std::ofstream csv(cfg.csvPath);
        if (csv.is_open()) {
            csv << "mode,count,repeat,rss_peak_kb,heap_peak_bytes\n";
            csv << (cfg.mode.empty() ? "all" : cfg.mode) << ","
                << cfg.count << ","
                << cfg.repeat << ","
                << rssPeak << ","
                << heapPeak << "\n";
        }
    }

    return 0;
}
