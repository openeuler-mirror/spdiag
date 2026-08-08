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

// 三档对比基准：
//   Case A: spdiag 已 start（共享内存存在，PerfPoint 处于 active 热路径）
//           循环 N 次 pt.Start()/pt.End() 的总耗时
//   Case B: spdiag 已 stop（共享内存不存在，PerfPoint 走 fast-return 静默路径）
//           循环 N 次 pt.Start()/pt.End() 的总耗时
//   Case C: 完全不调用 pt.Start()/pt.End()，相同循环体的总耗时（baseline）
//
// 使用方法：
//   1) 测 Case A: 先 ./spdiag start，再运行本程序
//   2) 测 Case B: 先 ./spdiag stop （或保持未 start 状态），再运行本程序
//   程序会自动检测共享内存状态，并标注当前命中的是 Case A 还是 Case B
//   Case C 始终在本程序内部完成，无需外部命令切换
//
// 参数： -n <iterations>  默认 1000000
//        -r <rounds>      重复轮数，默认 5（取中位数）

#include "spdiag/perf_point.h"
#include "spdiag/perf_key_defined.h"
#include "spdiag/perf_manager.h"
#include "spdiag/perf_config.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>

#include <sched.h>
#include <pthread.h>

using namespace SpDiag;

// 编译器屏障：阻止编译器消除空循环或重排内存访问，但不发射任何指令。
// 用于 Case C 保留纯循环骨架（i++/比较/分支）作为 baseline。
#if defined(__GNUC__) || defined(__clang__)
    #define SPDIAG_COMPILER_BARRIER() __asm__ volatile("" ::: "memory")
#else
    #define SPDIAG_COMPILER_BARRIER() _ReadWriteBarrier()
#endif

// --------- 工具 ---------

static void BindToCore(int coreId) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(coreId, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
}

static uint64_t NowNs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

static double Median(std::vector<uint64_t> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return (n % 2 == 1) ? static_cast<double>(v[n / 2])
                        : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

// --------- 三档被测循环 ---------

// Case A/B: 在循环中执行 Start()/End()。
// active 状态由 PerfPoint 构造时确定，受共享内存是否存在决定。
static uint64_t LoopWithPerfPoint(uint64_t n, PerfPoint& pt) {
    uint64_t t0 = NowNs();
    for (uint64_t i = 0; i < n; ++i) {
        pt.Start();
        pt.End(0);
    }
    return NowNs() - t0;
}

// Case C: 同样的循环结构，但不调用 Start()/End()，循环体为空。
// 用编译器屏障阻止编译器把空循环优化掉，保留纯循环骨架（i++/比较/分支）。
static uint64_t LoopWithoutPerfPoint(uint64_t n) {
    uint64_t t0 = NowNs();
    for (uint64_t i = 0; i < n; ++i) {
        SPDIAG_COMPILER_BARRIER();
    }
    return NowNs() - t0;
}

// --------- 主流程 ---------

int main(int argc, char* argv[]) {
    uint64_t iterations = 1000000;
    int rounds = 5;
    int coreId = 0;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            iterations = std::stoull(argv[++i]);
        } else if (std::strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            rounds = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--core") == 0 && i + 1 < argc) {
            coreId = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            std::cout << "Usage: benchmark_start_stop_overhead [-n iterations] [-r rounds] [--core N]\n"
                      << "  -n          Iterations per loop (default 1000000)\n"
                      << "  -r          Repeat rounds, report median (default 5)\n"
                      << "  --core N    Bind to CPU core N (default 0)\n"
                      << "\nExternal SHM state decides Case A vs Case B:\n"
                      << "  Case A: run `spdiag start` BEFORE this program\n"
                      << "  Case B: ensure `spdiag stop` (or never started) BEFORE this program\n";
            return 0;
        }
    }

    // 绑核以减少噪声
    BindToCore(coreId);

    std::cout << "=== SpDiag Start/Stop Overhead Benchmark ===\n";
    std::cout << "  Iterations per loop: " << iterations << "\n";
    std::cout << "  Rounds:              " << rounds << "\n";
    std::cout << "  Bind core:           " << coreId << "\n\n";

    // 关键：构造一个 PerfPoint。active_ 在构造时被确定。
    //   - 若 SHM 存在且注册成功  -> active_=true  -> Case A 热路径
    //   - 若 SHM 不存在          -> active_=false -> Case B 静默 fast-return
    // 根据 project 约束，active_ 一旦确定就不会动态变更（Windows 无 watcher；
    // Linux 有 inotify watcher，但本程序不重启不会重新激活已存在的 PerfPoint 实例）。
    PerfPoint pt(PerfKeyDefined::PROCESS_WORK, PerfLevel::KEY_MODULE);

    const bool shmActive = PerfManager::Instance().IsInitialized();
    std::cout << "  Shared memory state: " << (shmActive ? "ACTIVE (spdiag started)"
                                                          : "ABSENT (spdiag stopped)")
              << "\n";
    std::cout << "  => PerfPoint active_ = " << (shmActive ? "true  [Case A]"
                                                            : "false [Case B]")
              << "\n\n";

    // 预热：让缓存/分支预测器进入稳态
    for (int i = 0; i < 100; ++i) {
        pt.Start();
        pt.End(0);
    }
    for (int i = 0; i < 100; ++i) {
        SPDIAG_COMPILER_BARRIER();
    }

    std::vector<uint64_t> withTimes;     // Case A 或 B 的多轮结果
    std::vector<uint64_t> withoutTimes;  // Case C 的多轮结果

    for (int r = 0; r < rounds; ++r) {
        // 交替执行，降低温度/频率漂移造成的系统性偏差
        uint64_t t_with = LoopWithPerfPoint(iterations, pt);
        uint64_t t_without = LoopWithoutPerfPoint(iterations);
        withTimes.push_back(t_with);
        withoutTimes.push_back(t_without);
        std::cout << "  round " << (r + 1) << "/" << rounds
                  << "  with=" << (t_with / 1000000.0) << " ms"
                  << "  without=" << (t_without / 1000000.0) << " ms\n";
    }

    double medWith = Median(withTimes);
    double medWithout = Median(withoutTimes);
    double avgWithPerIter = medWith / static_cast<double>(iterations);
    double avgWithoutPerIter = medWithout / static_cast<double>(iterations);
    double overheadNs = avgWithPerIter - avgWithoutPerIter;
    double overheadPct = (avgWithoutPerIter > 0.0)
                             ? (overheadNs / avgWithoutPerIter) * 100.0
                             : 0.0;

    // 输出汇总
    std::cout << "\n=== Results (median over " << rounds << " rounds) ===\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "  Case C (no Start/End)        total: " << (medWithout / 1000000.0)
              << " ms   per-iter: " << avgWithoutPerIter << " ns\n";
    std::cout << "  Case " << (shmActive ? 'A' : 'B')
              << " (Start/End, shm " << (shmActive ? "ACTIVE" : "ABSENT") << ")   total: "
              << (medWith / 1000000.0)
              << " ms   per-iter: " << avgWithPerIter << " ns\n";
    std::cout << "  Overhead per iter: " << overheadNs << " ns"
              << "  (" << overheadPct << "% vs Case C)\n";

    // 结论性提示
    std::cout << "\n=== Conclusion ===\n";
    if (shmActive) {
        std::cout << "  [Case A] spdiag started: Start()/End() hot-path overhead = "
                  << overheadNs << " ns/iter\n";
        std::cout << "  Run `spdiag stop` (or start from a clean state) and re-run to get Case B.\n";
    } else {
        std::cout << "  [Case B] spdiag stopped: Start()/End() silent-path overhead = "
                  << overheadNs << " ns/iter\n";
        std::cout << "  Run `spdiag start` BEFORE launching this program to get Case A.\n";
    }
    std::cout << "  [Case C] baseline loop without Start()/End(): "
              << avgWithoutPerIter << " ns/iter\n";

    return 0;
}
