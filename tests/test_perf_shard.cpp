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
#include "spdiag/perf_shard.h"
#include "spdiag/shm_layout.h"

TEST(PerfShardTest, InitAndNumCores) {
    SpDiag::PerfShardManager mgr;
    mgr.Init(4);
    EXPECT_EQ(mgr.GetNumCores(), 4u);
}

TEST(PerfShardTest, ShardArrayAndCoreId) {
    SpDiag::PerfShardManager mgr;
    mgr.Init(4);

    SpDiag::LocalPerfShard* arr = mgr.GetShardArray();
    ASSERT_NE(arr, nullptr);
    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_EQ(arr[i].coreId, i);
    }
}

TEST(PerfShardTest, DirectStatAccess) {
    SpDiag::PerfShardManager mgr;
    mgr.Init(2);

    SpDiag::LocalPerfShard& shard = mgr.GetShard();
    shard.stats[5].backGoodCount = 42;
    shard.stats[5].slotIndex = 5;

    EXPECT_EQ(shard.stats[5].backGoodCount, 42u);
    EXPECT_EQ(shard.stats[5].slotIndex, 5u);
}

TEST(PerfShardTest, LocalPerfShardReset) {
    SpDiag::PerfShardManager mgr;
    mgr.Init(1);

    SpDiag::LocalPerfShard& shard = mgr.GetShard();
    shard.coreId = 99;
    shard.stats[0].backGoodCount = 100;
    shard.stats[3].tickCount = 50;
    shard.stats[3].totalTimeNs = 999;

    shard.reset();
    EXPECT_EQ(shard.coreId, 0u);
    EXPECT_EQ(shard.stats[0].backGoodCount, 0u);
    EXPECT_EQ(shard.stats[3].tickCount, 0u);
    EXPECT_EQ(shard.stats[3].totalTimeNs, 0u);
}

TEST(PerfShardTest, LocalPerfShardCopy) {
    SpDiag::LocalPerfShard src{};
    src.coreId = 7;
    src.stats[2].backGoodCount = 33;
    src.stats[2].tickCount = 33;
    src.stats[2].totalTimeNs = 1000;

    SpDiag::LocalPerfShard dst{};
    dst.copy(src);
    EXPECT_EQ(dst.coreId, 7u);
    EXPECT_EQ(dst.stats[2].backGoodCount, 33u);
    EXPECT_EQ(dst.stats[2].tickCount, 33u);
    EXPECT_EQ(dst.stats[2].totalTimeNs, 1000u);
}

TEST(PerfShardTest, SnapshotAndReset) {
    SpDiag::PerfShardManager mgr;
    mgr.Init(1);

    SpDiag::LocalPerfShard& shard = mgr.GetShard();
    shard.stats[1].backGoodCount = 10;
    shard.stats[1].tickCount = 10;

    SpDiag::LocalPerfShard snapshot{};
    mgr.SnapshotAndReset(snapshot, shard);

    EXPECT_EQ(snapshot.stats[1].backGoodCount, 10u);
    EXPECT_EQ(snapshot.stats[1].tickCount, 10u);
    EXPECT_EQ(shard.stats[1].backGoodCount, 0u);
    EXPECT_EQ(shard.stats[1].tickCount, 0u);
}
