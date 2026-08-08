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

// 全局 PerfPoint 单元/集成测试。
// 自建 v3 SHM（经 SpDiagManager），不依赖外部 `spdiag start`，故 ctest 直接可跑。
#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <cstring>
#include "manager/manager.h"
#include "cli/cli_config.h"
#include "spdiag/perf_point.h"
#include "spdiag/perf_key_defined.h"
#include "spdiag/perf_manager.h"
#include "spdiag/perf_config.h"
#include "spdiag/shm_layout.h"
#include "sdk/global_perf_point_factory.h"
#include "runtime/shm_reader.h"

namespace {

constexpr const char* kShmName = "/spdiag_test_global_pp";

SpDiag::Cli::CliConfig MakeStartCfg(const char* name) {
    SpDiag::Cli::CliConfig c;
    c.shmName = name;
    c.numCores = 4;
    c.maxSlots = SpDiag::MAX_PERF_SLOTS;   // 256，与 SDK 数组一致
    c.maxPrograms = 8;
    c.dumpIntervalMs = 60000;              // 测试期间不主动 dump
    c.levelGate = 4;
    return c;
}

const SpDiag::GlobalAccum* AccumFor(SpDiag::PerfKeyDefined key) {
    uint32_t gslot = SpDiag::PerfManager::Instance().FastResolveSlot(
        static_cast<uint32_t>(key));
    auto& fac = SpDiag::GlobalPerfPointFactory::Instance();
    SpDiag::GlobalAccum* arr = fac.Array();
    if (arr == nullptr || gslot >= fac.Count()) {
        return nullptr;
    }
    return &arr[gslot];
}

void BusyWork(int n = 5000) {
    for (volatile int i = 0; i < n; ++i) {
    }
}

}  // namespace

class GlobalPerfPointTest : public ::testing::Test {
protected:
    void SetUp() override {
        using namespace SpDiag;
        // 清理残留 SHM，确保干净起点。
        {
            Cli::CliConfig c = MakeStartCfg(kShmName);
            c.command = Cli::Command::STOP;
            SpDiagManager m;
            m.StopShm(c);
        }
        {
            Cli::CliConfig c = MakeStartCfg(kShmName);
            c.command = Cli::Command::START;
            SpDiagManager m;
            ASSERT_TRUE(m.StartShm(c)) << "failed to create test SHM";
        }

        PerfConfig pcfg;
        pcfg.shmName = kShmName;
        pcfg.mode = RunMode::DEBUG;
        pcfg.levelGate = 4;
        pcfg.dumpIntervalMs = 60000;
        PerfManager::Instance().Init(pcfg);
        ASSERT_TRUE(PerfManager::Instance().IsInitialized()) << "PerfManager init failed";
    }

    void TearDown() override {
        using namespace SpDiag;
        PerfManager::Instance().Shutdown();
        Cli::CliConfig c = MakeStartCfg(kShmName);
        c.command = Cli::Command::STOP;
        SpDiagManager m;
        m.StopShm(c);
    }
};

TEST_F(GlobalPerfPointTest, StartEndAccumulates) {
    using namespace SpDiag;
    {
        PerfPoint pt(PerfKeyDefined::COMPUTE_MATMUL, global_perf);
        pt.Start();
        BusyWork();
        pt.End(0);
    }
    const GlobalAccum* a = AccumFor(PerfKeyDefined::COMPUTE_MATMUL);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->goodCount.load(), 1u);
    EXPECT_EQ(a->tickCount.load(), 1u);
    EXPECT_GT(a->totalTimeNs.load(), 0u);
}

TEST_F(GlobalPerfPointTest, BadReturnCode) {
    using namespace SpDiag;
    {
        PerfPoint pt(PerfKeyDefined::COMPUTE_PRIME, global_perf);
        pt.Start();
        pt.End(1);
    }
    const GlobalAccum* a = AccumFor(PerfKeyDefined::COMPUTE_PRIME);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->badCount.load(), 1u);
    EXPECT_EQ(a->goodCount.load(), 0u);
}

TEST_F(GlobalPerfPointTest, AbandonOnDestruct) {
    using namespace SpDiag;
    {
        PerfPoint pt(PerfKeyDefined::IO_IOTASK, global_perf);
        pt.Start();
        // 不调用 End，析构应走 Abandon。
    }
    const GlobalAccum* a = AccumFor(PerfKeyDefined::IO_IOTASK);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->notCount.load(), 1u);
    EXPECT_EQ(a->tickCount.load(), 1u);
}

TEST_F(GlobalPerfPointTest, RepeatedUseAccumulates) {
    using namespace SpDiag;
    PerfPoint pt(PerfKeyDefined::COMPUTE_MATMUL, global_perf);
    for (int i = 0; i < 100; ++i) {
        pt.Start();
        pt.End(0);
    }
    const GlobalAccum* a = AccumFor(PerfKeyDefined::COMPUTE_MATMUL);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->tickCount.load(), 100u);
    EXPECT_EQ(a->goodCount.load(), 100u);
}

TEST_F(GlobalPerfPointTest, ConcurrentAccumulation) {
    using namespace SpDiag;
    constexpr int N_THREADS = 16;
    constexpr int ITER = 1000;
    std::vector<std::thread> threads;
    for (int t = 0; t < N_THREADS; ++t) {
        threads.emplace_back([&]() {
            // 各线程自有对象，但同 key → 工厂类同一原子累加器（flyweight）。
            PerfPoint pt(PerfKeyDefined::COMPUTE_PRIME, global_perf);
            for (int i = 0; i < ITER; ++i) {
                pt.Start();
                pt.End(0);
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    const GlobalAccum* a = AccumFor(PerfKeyDefined::COMPUTE_PRIME);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->tickCount.load(), static_cast<uint64_t>(N_THREADS) * ITER);
    EXPECT_EQ(a->goodCount.load(), static_cast<uint64_t>(N_THREADS) * ITER);
}

TEST_F(GlobalPerfPointTest, CrossThreadHandoff) {
    // 核心场景：线程 A Start、线程 B End（同一对象，经 mutex/cv 同步发布）。
    using namespace SpDiag;
    PerfPoint pt(PerfKeyDefined::COMPUTE_PRIME, global_perf);

    std::mutex m;
    std::condition_variable cv;
    bool started = false;

    std::thread worker([&]() {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&] { return started; });
        BusyWork();   // 让 elapsed 有可测量的大小
        pt.End(0);   // 线程 B End：acquire 读到 A 写的 startTsGlobal_
    });
    {
        std::lock_guard<std::mutex> lk(m);
        pt.Start();   // 线程 A Start：release 写 startTsGlobal_
        started = true;
    }
    cv.notify_one();
    worker.join();

    const GlobalAccum* a = AccumFor(PerfKeyDefined::COMPUTE_PRIME);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->tickCount.load(), 1u);
    EXPECT_EQ(a->goodCount.load(), 1u);
    EXPECT_GT(a->totalTimeNs.load(), 0u);   // 跨线程 handoff 耗时落地
}

TEST_F(GlobalPerfPointTest, OutOfBoundsDegrades) {
    // slotIndex 使 globalSlot >= MAX_GLOBAL_POINTS → Acquire 返回 nullptr → no-op，不崩不越界。
    using namespace SpDiag;
    PerfPoint pt(static_cast<uint32_t>(MAX_GLOBAL_POINTS) + 10u, global_perf);
    pt.Start();
    pt.End(0);
    pt.Abandon();
    // 一个合法的全局点仍能正常累加，证明全局状态未被越界写破坏。
    {
        PerfPoint good(PerfKeyDefined::COMPUTE_MATMUL, global_perf);
        good.Start();
        good.End(0);
    }
    const GlobalAccum* a = AccumFor(PerfKeyDefined::COMPUTE_MATMUL);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->goodCount.load(), 1u);
}

TEST_F(GlobalPerfPointTest, MinMaxTracked) {
    // 验证 CAS 维护的 min/max：多次测量后 min<=avg<=max，且一次长测量拉开 max。
    using namespace SpDiag;
    PerfPoint pt(PerfKeyDefined::COMPUTE_MATMUL, global_perf);
    for (int i = 0; i < 5; ++i) {
        pt.Start();
        BusyWork(1000);
        pt.End(0);
    }
    pt.Start();
    BusyWork(3000000);   // 明显更长的一次测量
    pt.End(0);
    const GlobalAccum* a = AccumFor(PerfKeyDefined::COMPUTE_MATMUL);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->tickCount.load(), 6u);
    EXPECT_GT(a->minTimeNs.load(), 0u);
    EXPECT_GT(a->maxTimeNs.load(), 0u);
    EXPECT_LE(a->minTimeNs.load(), a->maxTimeNs.load());
    uint64_t avg = a->totalTimeNs.load() / a->tickCount.load();
    EXPECT_GE(avg, a->minTimeNs.load());
    EXPECT_LE(avg, a->maxTimeNs.load());
}

TEST_F(GlobalPerfPointTest, ReStartAbandonsPrevious) {
    // 未 End 再 Start：前一次在途测量应被 Abandon（notCount++）。
    using namespace SpDiag;
    PerfPoint pt(PerfKeyDefined::COMPUTE_PRIME, global_perf);
    pt.Start();
    BusyWork();
    pt.Start();   // 放弃第一次
    BusyWork();
    pt.End(0);    // 第二次正常 End
    const GlobalAccum* a = AccumFor(PerfKeyDefined::COMPUTE_PRIME);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->notCount.load(), 1u);   // 第一次被 abandon
    EXPECT_EQ(a->goodCount.load(), 1u);  // 第二次 good
    EXPECT_EQ(a->tickCount.load(), 2u);  // abandon(1) + end(1)
}

TEST_F(GlobalPerfPointTest, ExplicitAbandon) {
    using namespace SpDiag;
    PerfPoint pt(PerfKeyDefined::PIPELINE_STAGE1, global_perf);
    pt.Start();
    pt.Abandon();
    const GlobalAccum* a = AccumFor(PerfKeyDefined::PIPELINE_STAGE1);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->notCount.load(), 1u);
    EXPECT_EQ(a->tickCount.load(), 1u);
    EXPECT_EQ(a->goodCount.load(), 0u);
}

TEST_F(GlobalPerfPointTest, LevelFiltering) {
    // level(99) > levelGate(4) → 不启用，Start/End 为 no-op。
    using namespace SpDiag;
    {
        PerfPoint pt(PerfKeyDefined::COMPUTE_MATMUL, global_perf, static_cast<PerfLevel>(99));
        pt.Start();
        BusyWork();
        pt.End(0);
    }
    const GlobalAccum* a = AccumFor(PerfKeyDefined::COMPUTE_MATMUL);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->tickCount.load(), 0u);
    EXPECT_EQ(a->goodCount.load(), 0u);
}

TEST_F(GlobalPerfPointTest, DifferentKeysIsolated) {
    // 不同 key 落到不同累加器，互不串扰。
    using namespace SpDiag;
    {
        PerfPoint a(PerfKeyDefined::COMPUTE_MATMUL, global_perf);
        a.Start();
        a.End(0);
    }
    {
        PerfPoint b(PerfKeyDefined::COMPUTE_PRIME, global_perf);
        b.Start();
        b.End(0);
        b.Start();
        b.End(0);
    }
    const GlobalAccum* aa = AccumFor(PerfKeyDefined::COMPUTE_MATMUL);
    const GlobalAccum* ab = AccumFor(PerfKeyDefined::COMPUTE_PRIME);
    ASSERT_NE(aa, nullptr);
    ASSERT_NE(ab, nullptr);
    EXPECT_NE(static_cast<const void*>(aa), static_cast<const void*>(ab));
    EXPECT_EQ(aa->goodCount.load(), 1u);
    EXPECT_EQ(ab->goodCount.load(), 2u);
}

TEST_F(GlobalPerfPointTest, CliReadsGlobalRegion) {
    // 端到端：全局点 → 进程内累加 → Shutdown 末次 dump 刷进 SHM 全局区 → CLI 读到。
    using namespace SpDiag;
    uint32_t gslot = PerfManager::Instance().FastResolveSlot(
        static_cast<uint32_t>(PerfKeyDefined::COMPUTE_MATMUL));
    {
        PerfPoint pt(PerfKeyDefined::COMPUTE_MATMUL, global_perf);
        for (int i = 0; i < 5; ++i) {
            pt.Start();
            BusyWork();
            pt.End(0);
        }
    }
    PerfManager::Instance().Shutdown();   // 触发末次 dump（factory 此时仍有效）

    ShmReader reader;
    ASSERT_TRUE(reader.Open(kShmName));
    const PerfStat* g = reader.ReadGlobalAccum();
    ASSERT_NE(g, nullptr);
    EXPECT_GT(reader.Header().globalAccumCount, 0u);
    EXPECT_GT(reader.Header().globalAccumOffset, reader.Header().shardsOffset);
    EXPECT_GT(g[gslot].tickCount, 0u);
    reader.Close();
    // TearDown 的 Shutdown 幂等；SHM 仍在（SDK 仅 unmap）。
}

TEST_F(GlobalPerfPointTest, GlobalAggregatesAlongsideLocal) {
    // 同 key 同时发 local + global，聚合后 tickCount == local + global。
    using namespace SpDiag;
    const auto key = PerfKeyDefined::COMPUTE_MATMUL;
    uint32_t gslot = PerfManager::Instance().FastResolveSlot(static_cast<uint32_t>(key));

    for (int i = 0; i < 3; ++i) {
        PerfPoint local(key, PerfLevel::SUB_SYSTEM);   // 本地点
        local.Start();
        BusyWork();
        local.End(0);
    }
    for (int i = 0; i < 2; ++i) {
        PerfPoint gp(key, global_perf);                 // 全局点
        gp.Start();
        BusyWork();
        gp.End(0);
    }
    PerfManager::Instance().Shutdown();   // 末次 dump：local shards + global 区都刷进 SHM

    ShmReader reader;
    ASSERT_TRUE(reader.Open(kShmName));
    const ShmHeader& hdr = reader.Header();
    ShardBuffer shards;
    std::vector<uint8_t> globalData;
    ASSERT_TRUE(reader.SnapshotShards(shards, &globalData));
    const ProgramEntry* programs = reader.ReadProgramRegistry();
    const char (*keyNames)[nameLen] = reader.ReadKeyNames();
    const char (*keyModNames)[nameLen] = reader.ReadKeyModuleNames();
    Aggregator agg;
    agg.Aggregate(programs, hdr.registeredPrograms, keyNames, keyModNames,
                  shards, hdr.numCores,
                  globalData.empty() ? nullptr
                                     : reinterpret_cast<const PerfStat*>(globalData.data()),
                  hdr.globalAccumCount);
    reader.Close();

    uint64_t total = 0;
    for (const auto& s : agg.Results()) {
        if (s.globalSlotIndex == gslot) {
            total = s.totalTickCount;
            break;
        }
    }
    EXPECT_EQ(total, 5u);   // 3 local + 2 global
}
