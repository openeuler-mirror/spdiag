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

// 全局 PerfPoint 能力演示：
//   1) 本地点 vs 全局点 对照（同一 key 两种构造）
//   2) 跨线程 handoff：线程 A Start(core 0) → 线程 B End(core 1)
//   3) 并发累加：多线程同 key 并发 Start/End
//
// 运行前请先 `spdiag start`（创建 SHM）；运行后 `spdiag show` 可见全局点位
// （全局点无百分位，p99 列为 0 属正常）。
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <sched.h>
#include <unistd.h>

#include "spdiag/perf_point.h"
#include "spdiag/perf_key_defined.h"
#include "spdiag/perf_manager.h"
#include "spdiag/perf_config.h"
#include "sdk/global_perf_point_factory.h"

using namespace SpDiag;

static void SetAffinity(uint32_t core) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    sched_setaffinity(0, sizeof(cpuset), &cpuset);
}

static void BusyWork(int n = 200000) {
    volatile int sink = 0;
    for (int i = 0; i < n; ++i) {
        sink += i;
    }
}

static const GlobalAccum* GetAccum(PerfKeyDefined key) {
    uint32_t gslot = PerfManager::Instance().FastResolveSlot(static_cast<uint32_t>(key));
    auto& fac = GlobalPerfPointFactory::Instance();
    GlobalAccum* arr = fac.Array();
    if (arr == nullptr || gslot >= fac.Count()) {
        return nullptr;
    }
    return &arr[gslot];
}

int main() {
    PerfConfig cfg;
    cfg.shmName = "/spdiag_shm_default";
    cfg.mode = RunMode::DEBUG;
    cfg.levelGate = 4;
    cfg.dumpIntervalMs = 1000;
    PerfManager::Instance().Init(cfg);
    if (!PerfManager::Instance().IsInitialized()) {
        std::cerr << "请先运行 `spdiag start` 创建 SHM。\n";
        return 1;
    }

    // ── 1) 本地 vs 全局对照 ──
    std::cout << "=== 1) 本地点 vs 全局点 ===\n";
    {
        PerfPoint local(PerfKeyDefined::COMPUTE_MATMUL);   // 本地（默认）
        local.Start();
        BusyWork();
        local.End(0);
    }
    {
        PerfPoint gp(PerfKeyDefined::COMPUTE_MATMUL, global_perf);  // 全局
        gp.Start();
        BusyWork();
        gp.End(0);
    }
    if (const GlobalAccum* a = GetAccum(PerfKeyDefined::COMPUTE_MATMUL)) {
        std::cout << "  全局累加器 goodCount=" << a->goodCount.load()
                  << " tickCount=" << a->tickCount.load()
                  << " totalTimeNs=" << a->totalTimeNs.load() << "\n";
    }

    // ── 2) 跨线程 handoff：A 线程 Start / B 线程 End（同一对象）──
    std::cout << "=== 2) 跨线程 Start/End（A: core0 Start, B: core1 End）===\n";
    {
        PerfPoint pt(PerfKeyDefined::COMPUTE_PRIME, global_perf);
        std::mutex m;
        std::condition_variable cv;
        bool started = false;

        std::thread worker([&]() {
            SetAffinity(1);
            std::unique_lock<std::mutex> lk(m);
            cv.wait(lk, [&] { return started; });
            std::cout << "  线程 B (core1) End()\n";
            pt.End(0);
        });

        SetAffinity(0);
        {
            std::lock_guard<std::mutex> lk(m);
            pt.Start();
            started = true;
            std::cout << "  线程 A (core0) Start()\n";
        }
        cv.notify_one();
        worker.join();

        if (const GlobalAccum* a = GetAccum(PerfKeyDefined::COMPUTE_PRIME)) {
            std::cout << "  跨线程测量落地：goodCount=" << a->goodCount.load()
                      << " totalTimeNs=" << a->totalTimeNs.load() << "\n";
        }
    }

    // ── 3) 并发累加：多线程同 key 并发 Start/End（不同对象，同一累加器）──
    std::cout << "=== 3) 并发累加（8 线程 × 1000 次，同 key）===\n";
    {
        constexpr int N = 8;
        constexpr int ITER = 1000;
        std::vector<std::thread> ts;
        for (int t = 0; t < N; ++t) {
            ts.emplace_back([&, t]() {
                PerfPoint pt(PerfKeyDefined::PIPELINE_STAGE1, global_perf);
                for (int i = 0; i < ITER; ++i) {
                    pt.Start();
                    pt.End(0);
                }
                (void)t;
            });
        }
        for (auto& th : ts) {
            th.join();
        }
        if (const GlobalAccum* a = GetAccum(PerfKeyDefined::PIPELINE_STAGE1)) {
            std::cout << "  并发后 tickCount=" << a->tickCount.load()
                      << "（期望 " << N * ITER << "）\n";
        }
    }

    std::cout << "\n提示：等待 dump（" << cfg.dumpIntervalMs << "ms）后运行 `spdiag show` 查看全局点位。\n";

    // 给定时器一次 dump 机会，再优雅退出。
    std::this_thread::sleep_for(std::chrono::milliseconds(cfg.dumpIntervalMs + 200));
    PerfManager::Instance().Shutdown();
    return 0;
}
