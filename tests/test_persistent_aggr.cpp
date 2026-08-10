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
#include "src/manager/persistent_aggr.h"
#include <filesystem>
#include <vector>

namespace {

class PersistentAggrTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDir_ = (std::filesystem::temp_directory_path() / "test_persistent_aggr").string();
        std::filesystem::remove_all(testDir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(testDir_);
    }

    std::string testDir_;
};

SpDiag::AggregatedStat MakeStat(
    uint32_t slotIdx,
    const std::string& mod,
    const std::string& pt,
    uint64_t tickCount,
    uint64_t timeNs,
    uint64_t goodCount = 0,
    uint64_t badCount = 0,
    uint64_t notCount = 0,
    uint64_t minNs = 0,
    uint64_t maxNs = 0) {
    SpDiag::AggregatedStat s;
    s.globalSlotIndex = slotIdx;
    s.moduleName = mod;
    s.pointName = pt;
    s.totalGoodCount = goodCount;
    s.totalBadCount = badCount;
    s.totalNotCount = notCount;
    s.totalTickCount = tickCount;
    s.totalTimeNs = timeNs;
    s.globalMinTimeNs = minNs;
    s.globalMaxTimeNs = maxNs;
    if (tickCount > 0) {
        s.avgTimeNs = static_cast<double>(timeNs) / static_cast<double>(tickCount);
    }
    return s;
}

}

TEST_F(PersistentAggrTest, FirstUpdateCreatesFiles) {
    SpDiag::PersistentAggr pa(testDir_);
    EXPECT_FALSE(pa.Exists());

    auto s1 = MakeStat(1, "mod", "pt", 100, 1000, 80, 10, 10, 5, 50);
    pa.Update({s1});

    EXPECT_TRUE(pa.Exists());

    auto loaded = pa.Load();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].globalSlotIndex, 1u);
    EXPECT_EQ(loaded[0].totalTickCount, 100u);
    EXPECT_EQ(loaded[0].totalTimeNs, 1000u);
    EXPECT_EQ(loaded[0].totalGoodCount, 80u);
    EXPECT_EQ(loaded[0].totalBadCount, 10u);
    EXPECT_EQ(loaded[0].totalNotCount, 10u);
    EXPECT_EQ(loaded[0].globalMinTimeNs, 5u);
    EXPECT_EQ(loaded[0].globalMaxTimeNs, 50u);
    EXPECT_EQ(loaded[0].moduleName, "mod");
    EXPECT_EQ(loaded[0].pointName, "pt");
}

TEST_F(PersistentAggrTest, IncrementalUpdate) {
    SpDiag::PersistentAggr pa(testDir_);

    auto s1 = MakeStat(1, "mod", "pt", 100, 1000, 80, 10, 10, 5, 50);
    pa.Update({s1});

    auto s2 = MakeStat(1, "mod", "pt", 150, 1800, 120, 15, 15, 3, 60);
    pa.Update({s2});

    auto loaded = pa.Load();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].totalTickCount, 150u);
    EXPECT_EQ(loaded[0].totalGoodCount, 120u);
    EXPECT_EQ(loaded[0].totalBadCount, 15u);
    EXPECT_EQ(loaded[0].totalNotCount, 15u);
    EXPECT_EQ(loaded[0].totalTimeNs, 1800u);
    EXPECT_NEAR(loaded[0].avgTimeNs, 1800.0 / 150.0, 1e-9);
}

TEST_F(PersistentAggrTest, ClearDetection) {
    SpDiag::PersistentAggr pa(testDir_);

    auto s1 = MakeStat(1, "mod", "pt", 100, 1000, 80, 10, 10, 5, 50);
    pa.Update({s1});

    auto s2 = MakeStat(1, "mod", "pt", 50, 500, 40, 5, 5, 2, 30);
    pa.Update({s2});

    auto loaded = pa.Load();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].totalTickCount, 150u);
    EXPECT_EQ(loaded[0].totalGoodCount, 120u);
    EXPECT_EQ(loaded[0].totalBadCount, 15u);
    EXPECT_EQ(loaded[0].totalNotCount, 15u);
    EXPECT_EQ(loaded[0].totalTimeNs, 1500u);
}

TEST_F(PersistentAggrTest, NewSlotIndex) {
    SpDiag::PersistentAggr pa(testDir_);

    auto s1 = MakeStat(1, "mod1", "pt1", 100, 1000, 80, 10, 10, 5, 50);
    pa.Update({s1});

    auto s1b = MakeStat(1, "mod1", "pt1", 120, 1200, 96, 12, 12, 4, 55);
    auto s2 = MakeStat(2, "mod2", "pt2", 200, 2000, 160, 20, 20, 10, 80);
    pa.Update({s1b, s2});

    auto loaded = pa.Load();
    ASSERT_EQ(loaded.size(), 2u);

    bool found1 = false, found2 = false;
    for (const auto& stat : loaded) {
        if (stat.globalSlotIndex == 1) {
            found1 = true;
            EXPECT_EQ(stat.totalTickCount, 120u);
            EXPECT_EQ(stat.totalTimeNs, 1200u);
            EXPECT_EQ(stat.moduleName, "mod1");
        }
        if (stat.globalSlotIndex == 2) {
            found2 = true;
            EXPECT_EQ(stat.totalTickCount, 200u);
            EXPECT_EQ(stat.totalTimeNs, 2000u);
            EXPECT_EQ(stat.moduleName, "mod2");
        }
    }
    EXPECT_TRUE(found1);
    EXPECT_TRUE(found2);
}
