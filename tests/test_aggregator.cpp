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
#include "spdiag/shm_layout.h"
#include "src/manager/aggregator.h"
#include <cstring>

namespace {

static constexpr uint32_t TEST_MAX_SLOTS = 16;
static constexpr uint32_t TEST_NUM_CORES = 2;

struct TestCtx {
    SpDiag::ProgramEntry programs[2]{};
    char keyPointNames[TEST_MAX_SLOTS][SpDiag::nameLen]{};
    char keyModuleNames[TEST_MAX_SLOTS][SpDiag::nameLen]{};
    SpDiag::ShardBuffer shards;
};

void InitSingleProgramCtx(TestCtx& ctx) {
    std::strncpy(ctx.programs[0].name, "TestProg", 63);
    ctx.programs[0].baseSlot = 0;
    ctx.programs[0].slotCount = TEST_MAX_SLOTS;
    ctx.programs[0].numCores = TEST_NUM_CORES;
    ctx.programs[0].active = 1;
    std::strncpy(ctx.keyPointNames[1], "TestPt", SpDiag::nameLen - 1);
    std::strncpy(ctx.keyModuleNames[1], "TestMod", SpDiag::nameLen - 1);
    ctx.shards.resize(1 * TEST_NUM_CORES, TEST_MAX_SLOTS);
}

void SetShardStat(SpDiag::ShardBuffer& shards, size_t programIdx, size_t coreIdx,
                  uint32_t, uint32_t localSlot, uint32_t good, uint32_t bad,
                  uint32_t notCount, uint32_t ticks, uint64_t totalNs,
                  uint64_t minNs, uint64_t maxNs) {
    (void)programIdx;
    SpDiag::PerfStat* stats = shards.stats(coreIdx);
    SpDiag::PerfStat& stat = stats[localSlot];
    stat.slotIndex = localSlot;
    stat.backGoodCount = good;
    stat.backBadCount = bad;
    stat.backNotCount = notCount;
    stat.tickCount = ticks;
    stat.totalTimeNs = totalNs;
    stat.minTimeNs = minNs;
    stat.maxTimeNs = maxNs;
}

}

TEST(AggregatorTest, CountAggregation) {
    TestCtx ctx{};
    InitSingleProgramCtx(ctx);

    SetShardStat(ctx.shards, 0, 0, TEST_MAX_SLOTS, 1, 10, 2, 1, 13, 1000, 50, 200);
    SetShardStat(ctx.shards, 0, 1, TEST_MAX_SLOTS, 1, 20, 3, 2, 25, 2000, 30, 300);

    SpDiag::Aggregator agg;
    agg.Aggregate(ctx.programs, 1, ctx.keyPointNames, ctx.keyModuleNames,
                  ctx.shards, TEST_NUM_CORES);

    const auto& results = agg.Results();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].totalGoodCount, 30u);
    EXPECT_EQ(results[0].totalBadCount, 5u);
    EXPECT_EQ(results[0].totalNotCount, 3u);
    EXPECT_EQ(results[0].totalTickCount, 38u);
    EXPECT_EQ(results[0].programName, "TestProg");
    EXPECT_EQ(results[0].moduleName, "TestMod");
    EXPECT_EQ(results[0].pointName, "TestPt");
}

TEST(AggregatorTest, MinMaxAggregation) {
    TestCtx ctx{};
    InitSingleProgramCtx(ctx);

    SetShardStat(ctx.shards, 0, 0, TEST_MAX_SLOTS, 1, 0, 0, 0, 1, 100, 100, 100);
    SetShardStat(ctx.shards, 0, 1, TEST_MAX_SLOTS, 1, 0, 0, 0, 1, 200, 200, 200);

    SpDiag::Aggregator agg;
    agg.Aggregate(ctx.programs, 1, ctx.keyPointNames, ctx.keyModuleNames,
                  ctx.shards, TEST_NUM_CORES);

    const auto& results = agg.Results();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].globalMinTimeNs, 100u);
    EXPECT_EQ(results[0].globalMaxTimeNs, 200u);
    EXPECT_DOUBLE_EQ(results[0].avgTimeNs, 150.0);
}

TEST(AggregatorTest, ClampsCoreCountToAvailableShards) {
    TestCtx ctx{};
    InitSingleProgramCtx(ctx);
    ctx.shards.resize(1, TEST_MAX_SLOTS);
    SetShardStat(ctx.shards, 0, 0, TEST_MAX_SLOTS, 1, 7, 0, 0, 7, 700, 100, 100);

    SpDiag::Aggregator agg;
    agg.Aggregate(ctx.programs, 1, ctx.keyPointNames, ctx.keyModuleNames,
                  ctx.shards, TEST_NUM_CORES);

    ASSERT_EQ(agg.Results().size(), 1u);
    EXPECT_EQ(agg.Results()[0].totalGoodCount, 7u);
}

TEST(AggregatorTest, ClampsProgramSlotRangeToShardCapacity) {
    TestCtx ctx{};
    InitSingleProgramCtx(ctx);
    ctx.programs[0].slotCount = TEST_MAX_SLOTS + 8;
    SetShardStat(ctx.shards, 0, 0, TEST_MAX_SLOTS, 1, 5, 0, 0, 5, 500, 100, 100);

    SpDiag::Aggregator agg;
    agg.Aggregate(ctx.programs, 1, ctx.keyPointNames, ctx.keyModuleNames,
                  ctx.shards, TEST_NUM_CORES);

    ASSERT_EQ(agg.Results().size(), 1u);
    EXPECT_EQ(agg.Results()[0].globalSlotIndex, 1u);
}

TEST(AggregatorTest, IgnoresInvalidSlotIndex) {
    TestCtx ctx{};
    InitSingleProgramCtx(ctx);
    SpDiag::PerfStat* stats = ctx.shards.stats(0);
    stats[1].slotIndex = TEST_MAX_SLOTS + 1;
    stats[1].tickCount = 1;
    stats[1].totalTimeNs = 100;

    SpDiag::Aggregator agg;
    agg.Aggregate(ctx.programs, 1, ctx.keyPointNames, ctx.keyModuleNames,
                  ctx.shards, TEST_NUM_CORES);

    EXPECT_TRUE(agg.Results().empty());
}

#ifdef SPDIAG_ENABLE_PERCENTILE
TEST(AggregatorTest, TopNMerge) {
    TestCtx ctx{};
    InitSingleProgramCtx(ctx);

    SpDiag::PerfStat* stats0 = ctx.shards.stats(0);
    stats0[1].slotIndex = 1;
    stats0[1].tickCount = 3;
    stats0[1].totalTimeNs = 600;
    stats0[1].minTimeNs = 100;
    stats0[1].maxTimeNs = 300;
    stats0[1].topN[0] = 300;
    stats0[1].topN[1] = 200;
    stats0[1].topN[2] = 100;
    stats0[1].topNCount = 3;

    SpDiag::PerfStat* stats1 = ctx.shards.stats(1);
    stats1[1].slotIndex = 1;
    stats1[1].tickCount = 3;
    stats1[1].totalTimeNs = 900;
    stats1[1].minTimeNs = 150;
    stats1[1].maxTimeNs = 400;
    stats1[1].topN[0] = 400;
    stats1[1].topN[1] = 350;
    stats1[1].topN[2] = 150;
    stats1[1].topNCount = 3;

    SpDiag::Aggregator agg;
    agg.Aggregate(ctx.programs, 1, ctx.keyPointNames, ctx.keyModuleNames,
                  ctx.shards, TEST_NUM_CORES);

    const auto& results = agg.Results();
    ASSERT_EQ(results.size(), 1u);
    ASSERT_GE(results[0].globalTopN.size(), 3u);
    EXPECT_EQ(results[0].globalTopN[0], 400u);
    EXPECT_EQ(results[0].globalTopN[1], 350u);
    EXPECT_EQ(results[0].globalTopN[2], 300u);
}

TEST(AggregatorTest, PercentileCalculation) {
    TestCtx ctx{};
    InitSingleProgramCtx(ctx);

    SpDiag::PerfStat* stats0 = ctx.shards.stats(0);
    stats0[1].slotIndex = 1;
    stats0[1].tickCount = 100;
    stats0[1].totalTimeNs = 10000;
    stats0[1].minTimeNs = 10;
    stats0[1].maxTimeNs = 500;
    stats0[1].topN[0] = 500;
    stats0[1].topN[1] = 400;
    stats0[1].topN[2] = 300;
    stats0[1].topN[3] = 200;
    stats0[1].topN[4] = 100;
    stats0[1].topNCount = 5;

    SpDiag::Aggregator agg;
    agg.Aggregate(ctx.programs, 1, ctx.keyPointNames, ctx.keyModuleNames,
                  ctx.shards, TEST_NUM_CORES);

    const auto& results = agg.Results();
    ASSERT_EQ(results.size(), 1u);

    EXPECT_TRUE(results[0].hasP99);
    EXPECT_TRUE(results[0].hasP999);
    EXPECT_TRUE(results[0].hasP9999);

    EXPECT_EQ(results[0].p99Ns, 400u);
    EXPECT_EQ(results[0].p999Ns, 500u);
    EXPECT_EQ(results[0].p9999Ns, 500u);
}
#endif

TEST(AggregatorTest, MergeDeltaAccumulation) {
    TestCtx ctx{};
    InitSingleProgramCtx(ctx);

    SetShardStat(ctx.shards, 0, 0, TEST_MAX_SLOTS, 1, 10, 0, 0, 10, 1000, 50, 200);
#ifdef SPDIAG_ENABLE_PERCENTILE
    SpDiag::PerfStat* stats0 = ctx.shards.stats(0);
    stats0[1].topN[0] = 200;
    stats0[1].topN[1] = 150;
    stats0[1].topNCount = 2;
#endif

    SpDiag::Aggregator agg;
    agg.Aggregate(ctx.programs, 1, ctx.keyPointNames, ctx.keyModuleNames,
                  ctx.shards, TEST_NUM_CORES);

    SpDiag::AggregatedStat delta;
    delta.globalSlotIndex = 1;
    delta.totalGoodCount = 20;
    delta.totalBadCount = 5;
    delta.totalNotCount = 3;
    delta.totalTickCount = 28;
    delta.totalTimeNs = 2000;
    delta.globalMinTimeNs = 30;
    delta.globalMaxTimeNs = 350;
#ifdef SPDIAG_ENABLE_PERCENTILE
    delta.globalTopN = {350, 180};
    delta.p99Ns = 300;
    delta.p999Ns = 340;
    delta.p9999Ns = 350;
    delta.hasP99 = true;
    delta.hasP999 = true;
    delta.hasP9999 = true;
#endif

    agg.MergeDelta({delta});

    const auto& results = agg.Results();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].totalGoodCount, 30u);
    EXPECT_EQ(results[0].totalBadCount, 5u);
    EXPECT_EQ(results[0].totalNotCount, 3u);
    EXPECT_EQ(results[0].totalTickCount, 38u);
    EXPECT_EQ(results[0].totalTimeNs, 3000u);
    EXPECT_EQ(results[0].globalMinTimeNs, 30u);
    EXPECT_EQ(results[0].globalMaxTimeNs, 350u);
    EXPECT_DOUBLE_EQ(results[0].avgTimeNs, 3000.0 / 38.0);

#ifdef SPDIAG_ENABLE_PERCENTILE
    EXPECT_EQ(results[0].p99Ns, 300u);
    EXPECT_EQ(results[0].p999Ns, 340u);
    EXPECT_EQ(results[0].p9999Ns, 350u);
    EXPECT_TRUE(results[0].hasP99);
    EXPECT_TRUE(results[0].hasP999);
    EXPECT_TRUE(results[0].hasP9999);
#endif
}

TEST(AggregatorTest, CrossProgramAggregation) {
    static constexpr uint32_t PROG_SLOTS = 5;
    SpDiag::ProgramEntry programs[2]{};
    std::strncpy(programs[0].name, "ProgA", 63);
    programs[0].baseSlot = 0;
    programs[0].slotCount = PROG_SLOTS;
    programs[0].numCores = 1;
    programs[0].active = 1;
    std::strncpy(programs[1].name, "ProgB", 63);
    programs[1].baseSlot = PROG_SLOTS;
    programs[1].slotCount = PROG_SLOTS;
    programs[1].numCores = 1;
    programs[1].active = 1;

    char keyPointNames[TEST_MAX_SLOTS][SpDiag::nameLen]{};
    char keyModuleNames[TEST_MAX_SLOTS][SpDiag::nameLen]{};
    std::strncpy(keyPointNames[1], "PtA", SpDiag::nameLen - 1);
    std::strncpy(keyModuleNames[1], "ModA", SpDiag::nameLen - 1);
    std::strncpy(keyPointNames[6], "PtB", SpDiag::nameLen - 1);
    std::strncpy(keyModuleNames[6], "ModB", SpDiag::nameLen - 1);

    SpDiag::ShardBuffer shards;
    shards.resize(1, TEST_MAX_SLOTS);

    SpDiag::PerfStat* stats0 = shards.stats(0);
    stats0[1].slotIndex = 1;
    stats0[1].backGoodCount = 10;
    stats0[1].tickCount = 10;
    stats0[1].totalTimeNs = 100;
    stats0[1].minTimeNs = 10;
    stats0[1].maxTimeNs = 10;

    stats0[6].slotIndex = 6;
    stats0[6].backGoodCount = 20;
    stats0[6].tickCount = 20;
    stats0[6].totalTimeNs = 200;
    stats0[6].minTimeNs = 5;
    stats0[6].maxTimeNs = 15;

    SpDiag::Aggregator agg;
    agg.Aggregate(programs, 2, keyPointNames, keyModuleNames, shards, 1);

    const auto& results = agg.Results();
    ASSERT_EQ(results.size(), 2u);

    EXPECT_EQ(results[0].globalSlotIndex, 1u);
    EXPECT_EQ(results[0].programName, "ProgA");
    EXPECT_EQ(results[0].totalGoodCount, 10u);
    EXPECT_EQ(results[0].moduleName, "ModA");
    EXPECT_EQ(results[0].pointName, "PtA");

    EXPECT_EQ(results[1].globalSlotIndex, 6u);
    EXPECT_EQ(results[1].programName, "ProgB");
    EXPECT_EQ(results[1].totalGoodCount, 20u);
    EXPECT_EQ(results[1].moduleName, "ModB");
    EXPECT_EQ(results[1].pointName, "PtB");
}
