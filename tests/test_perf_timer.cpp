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

// PerfTimer 集成测试（经 PerfManager 驱动，不直接构造 PerfTimer）。
// 短 dumpIntervalMs 下经只读 ShmReader 观察 SHM header.dumpSeqNo 的周期推进与 Shutdown 后稳定。
// dumpSeqNo 每次 dump 经 CAS +1（偶→奇）再 fetch_add +1（奇→偶），故每轮 +2。
// PerfManager 未初始化（如 Windows 无 SHM）时各用例 GTEST_SKIP。
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include "manager/manager.h"
#include "cli/cli_config.h"
#include "sdk/global_perf_point_factory.h"
#include "spdiag/perf_manager.h"
#include "spdiag/perf_config.h"
#include "spdiag/perf_key_defined.h"
#include "spdiag/perf_level.h"
#include "spdiag/perf_point.h"
#include "spdiag/shm_layout.h"
#include "runtime/shm_reader.h"

namespace {

constexpr const char* kShmName = "/spdiag_test_perf_timer_unit";
constexpr uint32_t kDumpIntervalMs = 50;   // 短间隔，便于在毫秒级观察 dumpSeqNo 推进

SpDiag::Cli::CliConfig MakeStartCfg(const char* name) {
    SpDiag::Cli::CliConfig c;
    c.shmName = name;
    c.numCores = 4;
    c.maxSlots = SpDiag::MAX_PERF_SLOTS;
    c.maxPrograms = 8;
    c.dumpIntervalMs = kDumpIntervalMs;
    c.levelGate = 4;
    return c;
}

#ifdef SPDIAG_ENABLE_PERCENTILE
uint32_t CountTopN(const SpDiag::ShardBuffer& shards, uint32_t slot) {
    uint32_t count = 0;
    for (uint32_t core = 0; core < shards.size(); ++core)
        count += shards.topN(core, slot).count;
    return count;
}
#endif

}  // namespace

class PerfTimerTest : public ::testing::Test {
protected:
    void SetUp() override {
        using namespace SpDiag;
        // 清理残留 SHM，确保干净起点（dumpSeqNo 从 CreateShm 初值重新计数）。
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
            m.StartShm(c);   // 失败则后续 Init 失败，各用例 GTEST_SKIP
        }

        PerfConfig pcfg;
        pcfg.shmName = kShmName;
        pcfg.mode = RunMode::DEBUG;
        pcfg.levelGate = 4;
        pcfg.dumpIntervalMs = kDumpIntervalMs;
        PerfManager::Instance().Init(pcfg);   // Init 内启动 PerfTimer 后台线程
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

// 用例1：dumpIntervalMs=50 下，约 500ms 窗口内 dumpSeqNo 至少推进一次。
// 轮询至多 ~1s（20 × 50ms），提前命中即退出，兼顾慢 CI；断言 seq1 > seq0。
TEST_F(PerfTimerTest, DumpSeqNoAdvancesPeriodically) {
    using namespace SpDiag;
    if (!PerfManager::Instance().IsInitialized()) {
        GTEST_SKIP() << "PerfManager not initialized (SHM unavailable on this platform)";
    }
    ShmReader reader;
    ASSERT_TRUE(reader.Open(kShmName)) << "ShmReader::Open failed";

    const uint64_t seq0 = reader.Header().dumpSeqNo.load(std::memory_order_acquire);
    uint64_t seq1 = seq0;
    for (int i = 0; i < 20 && seq1 == seq0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        seq1 = reader.Header().dumpSeqNo.load(std::memory_order_acquire);
    }
    EXPECT_GT(seq1, seq0);
    reader.Close();
}

// 用例2：Shutdown 停止 timer。PerfTimer::Stop 会执行一次末次 DoOneDump（dumpSeqNo 可能 +2），
// 故捕获 Shutdown 后的值，再等待并断言其稳定不变（timer 线程已 join，不再有 writer）。
TEST_F(PerfTimerTest, ShutdownStopsTimer) {
    using namespace SpDiag;
    if (!PerfManager::Instance().IsInitialized()) {
        GTEST_SKIP() << "PerfManager not initialized (SHM unavailable on this platform)";
    }
    ShmReader reader;
    ASSERT_TRUE(reader.Open(kShmName)) << "ShmReader::Open failed";

    const uint64_t before = reader.Header().dumpSeqNo.load(std::memory_order_acquire);
    PerfManager::Instance().Shutdown();   // 停 timer，同步执行末次 DoOneDump
    const uint64_t after = reader.Header().dumpSeqNo.load(std::memory_order_acquire);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const uint64_t after2 = reader.Header().dumpSeqNo.load(std::memory_order_acquire);

    EXPECT_GE(after, before);     // 末次 dump 可能推进
    EXPECT_EQ(after, after2);     // timer 已停：再等待 dumpSeqNo 不变
    reader.Close();
}

TEST(PerfTimerOnDemandTest, ConcurrentClearUsesLazyGenerationHandoff) {
    using namespace SpDiag;
    const std::string name = "/spdiag_test_clear_handoff_" +
                             std::to_string(getpid());
    auto config = MakeStartCfg(name.c_str());
    config.dumpIntervalMs = 0;
    SpDiagManager manager;
    ASSERT_TRUE(manager.StartShm(config));
    PerfConfig perfConfig;
    perfConfig.shmName = name;
    perfConfig.levelGate = 4;
    PerfManager::Instance().Init(perfConfig);
    ASSERT_TRUE(PerfManager::Instance().IsInitialized());

    const uint32_t globalSlot = PerfManager::Instance().FastResolveSlot(
        static_cast<uint32_t>(PerfKeyDefined::COMPUTE_MATMUL));
    GlobalAccum& global = GlobalPerfPointFactory::Instance().Array()[globalSlot];
    ShmReader clearReader;
    ASSERT_TRUE(clearReader.Open(name));
    BeginGlobalAccumUpdate(global);
    global.goodCount.fetch_add(1, std::memory_order_relaxed);
    bool clearResult = false;
    std::atomic<bool> clearDone{false};
    std::thread clearThread([&]() {
        clearResult = manager.ClearData(config);
        clearDone.store(true, std::memory_order_release);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_FALSE(clearDone.load(std::memory_order_acquire));
    EXPECT_EQ(clearReader.Header().dumpSeqNo.load(std::memory_order_acquire) & 1U,
              0u);
    global.totalTimeNs.fetch_add(1, std::memory_order_relaxed);
    global.tickCount.fetch_add(1, std::memory_order_acq_rel);
    clearThread.join();
    ASSERT_TRUE(clearResult);
    clearReader.Close();
    ASSERT_TRUE(manager.RequestFreshDump(config));
    ShmReader reader;
    ASSERT_TRUE(reader.Open(name));
    const GlobalPerfStat* globalStats = reader.ReadGlobalAccum();
    ASSERT_NE(globalStats, nullptr);
    EXPECT_EQ(globalStats[globalSlot].stat.tickCount, 0u);
    EXPECT_EQ(globalStats[globalSlot].stat.backGoodCount, 0u);
    EXPECT_EQ(globalStats[globalSlot].stat.totalTimeNs, 0u);
    reader.Close();
    PerfManager::Instance().Shutdown();
    EXPECT_TRUE(manager.StopShm(config));
}

TEST(PerfTimerOnDemandTest, RequestPublishesOnceAndAcknowledges) {
    using namespace SpDiag;
    const std::string name = "/spdiag_test_on_demand_" +
                             std::to_string(getpid());
    auto config = MakeStartCfg(name.c_str());
    config.dumpIntervalMs = 0;
    SpDiagManager manager;
    ASSERT_TRUE(manager.StartShm(config));
    PerfConfig perfConfig;
    perfConfig.shmName = name;
    perfConfig.levelGate = 4;
    perfConfig.dumpIntervalMs = 0;
    PerfManager::Instance().Init(perfConfig);
    ASSERT_TRUE(PerfManager::Instance().IsInitialized());

    ShmReader reader;
    ASSERT_TRUE(reader.Open(name));
    const uint64_t before = reader.Header().dumpSeqNo.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_EQ(reader.Header().dumpSeqNo.load(), before);
    PerfPoint point(PerfKeyDefined::COMPUTE_MATMUL,
                    PerfLevel::SUB_SYSTEM);
    point.Start();
    point.End(0);
    ASSERT_TRUE(manager.RequestFreshDump(config));
    const uint64_t after = reader.Header().dumpSeqNo.load();
    EXPECT_GT(after, before);
    const ProgramEntry* registry = reader.ReadProgramRegistry();
    EXPECT_EQ(__atomic_load_n(&registry[0].dumpAckGeneration,
                              __ATOMIC_ACQUIRE),
              reader.Header().dumpRequestGeneration.load());
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_EQ(reader.Header().dumpSeqNo.load(), after);
    reader.Close();
    PerfManager::Instance().Shutdown();
    EXPECT_TRUE(manager.StopShm(config));
}

TEST(PerfTimerOnDemandTest, DumpWindowBudgetScalesWithCapacityAndWriters) {
    EXPECT_EQ(SpDiag::DumpWindowTimeoutMs(100, 1), 500u);
    EXPECT_EQ(SpDiag::DumpWindowTimeoutMs(1000, 8), 2400u);
    EXPECT_EQ(SpDiag::DumpWindowTimeoutMs(10000, 8), 9600u);
    EXPECT_EQ(SpDiag::DumpWindowTimeoutMs(10000, 100), 60000u);
}

#ifdef SPDIAG_ENABLE_PERCENTILE
TEST_F(PerfTimerTest, ClearDoesNotRepublishProcessLocalTopN) {
    using namespace SpDiag;
    if (!PerfManager::Instance().IsInitialized()) GTEST_SKIP();
    constexpr auto key = PerfKeyDefined::COMPUTE_MATMUL;
    const uint32_t slot = static_cast<uint32_t>(key);
    auto hit = [&]() {
        PerfPoint point(key, PerfLevel::SUB_SYSTEM);
        point.Start();
        point.End(0);
    };
    hit();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    SpDiagManager manager;
    ASSERT_TRUE(manager.ClearData(MakeStartCfg(kShmName)));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    ShmReader reader;
    ASSERT_TRUE(reader.Open(kShmName));
    ShardBuffer shards;
    ASSERT_TRUE(reader.SnapshotShards(shards));
    EXPECT_EQ(CountTopN(shards, slot), 0u);

    hit();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    ASSERT_TRUE(reader.SnapshotShards(shards));
    EXPECT_EQ(CountTopN(shards, slot), 1u);
}
#endif
