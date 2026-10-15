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

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <unistd.h>

#include "cli/cli_config.h"
#include "manager/manager.h"
#include "sdk/global_perf_point_factory.h"
#include "spdiag/perf_key_defined.h"
#include "spdiag/perf_manager.h"
#include "spdiag/perf_point.h"

namespace {

class PerfPointDurationTest : public ::testing::TestWithParam<int> {
protected:
    void SetUp() override {
        using namespace SpDiag;
        config_.shmName = "/spdiag_test_point_duration_" + std::to_string(getpid());
        config_.numCores = 1;  // Keep the local sample in one shard across CPU migrations.
        config_.maxSlots = MAX_PERF_SLOTS;
        config_.maxPrograms = 1;
        config_.dumpIntervalMs = 60000;  // Inspect samples before the periodic dump resets them.
        config_.levelGate = 4;
#ifdef SPDIAG_ENABLE_PERFLOG
        config_.perflogStart = true;
#endif
        SpDiagManager manager;
        ASSERT_TRUE(manager.StartShm(config_));

        PerfConfig perfConfig;
        perfConfig.shmName = config_.shmName;
        perfConfig.mode = RunMode::DEBUG;
        perfConfig.levelGate = 4;
        perfConfig.dumpIntervalMs = config_.dumpIntervalMs;
        PerfManager::Instance().Init(perfConfig);
        ASSERT_TRUE(PerfManager::Instance().IsInitialized());
    }

    void TearDown() override {
        SpDiag::PerfManager::Instance().Shutdown();
        SpDiag::SpDiagManager manager;
        manager.StopShm(config_);
    }

    SpDiag::Cli::CliConfig config_;
};

// Issue #12: a 64-bit intermediate in the Q32 conversion wraps near 4.295s.
// Exercise both real Start/End paths against an independent clock; use the
// actual elapsed time so scheduler delays do not change the expected result.
TEST_P(PerfPointDurationTest, LocalAndGlobalMatchSteadyClock) {
    using namespace SpDiag;
    constexpr auto key = PerfKeyDefined::COMPUTE_MATMUL;
    const auto slot = static_cast<uint32_t>(key);
    PerfPoint local(key, PerfLevel::SUB_SYSTEM);
    PerfPoint global(key, global_perf);

    const auto start = std::chrono::steady_clock::now();
    local.Start();
    global.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(GetParam()));
    global.End(0);
    local.End(0);
    const auto referenceNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
    // Allow timing overhead under emulation while still catching a 4.295s wrap.
    const auto toleranceNs = std::max<int64_t>(50000000, referenceNs / 100);

    const auto& stat = PerfManager::Instance().GetShard().stats[slot];
    EXPECT_EQ(stat.tickCount, 1u);
    EXPECT_EQ(stat.backGoodCount, 1u);
    EXPECT_EQ(stat.backBadCount, 0u);
    EXPECT_EQ(stat.backNotCount, 0u);
    EXPECT_NEAR(static_cast<double>(stat.totalTimeNs),
                static_cast<double>(referenceNs), static_cast<double>(toleranceNs));
    EXPECT_EQ(stat.minTimeNs, stat.totalTimeNs);
    EXPECT_EQ(stat.maxTimeNs, stat.totalTimeNs);
#ifdef SPDIAG_ENABLE_PERCENTILE
    ASSERT_EQ(stat.topNCount, 1u);
    EXPECT_EQ(stat.topN[0], stat.totalTimeNs);
#endif
#ifdef SPDIAG_ENABLE_PERFLOG
    ASSERT_TRUE(PerfManager::Instance().IsPerfLogEnabled());
    ASSERT_EQ(stat.perflogWritePos.load(), 1u);
    EXPECT_EQ(stat.perflogCosts[0], stat.totalTimeNs);
#endif

    const auto globalSlot = PerfManager::Instance().FastResolveSlot(slot);
    const auto& factory = GlobalPerfPointFactory::Instance();
    ASSERT_LT(globalSlot, factory.Count());
    const auto* accum = GlobalPerfPointFactory::Instance().Array() + globalSlot;
    EXPECT_EQ(accum->tickCount.load(), 1u);
    EXPECT_EQ(accum->goodCount.load(), 1u);
    EXPECT_EQ(accum->badCount.load(), 0u);
    EXPECT_EQ(accum->notCount.load(), 0u);
    EXPECT_NEAR(static_cast<double>(accum->totalTimeNs.load()),
                static_cast<double>(referenceNs), static_cast<double>(toleranceNs));
    EXPECT_EQ(accum->minTimeNs.load(), accum->totalTimeNs.load());
    EXPECT_EQ(accum->maxTimeNs.load(), accum->totalTimeNs.load());
}

INSTANTIATE_TEST_SUITE_P(
    OverflowBoundary, PerfPointDurationTest,
    ::testing::Values(1000, 4200, 4400, 5000, 10000),
    [](const ::testing::TestParamInfo<int>& info) {
        return "Sleep" + std::to_string(info.param) + "ms";
    });

}  // namespace
