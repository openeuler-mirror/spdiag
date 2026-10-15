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
#include "spdiag/perf_point.h"
#include "spdiag/perf_key_defined.h"
#include "spdiag/perf_manager.h"
#include "spdiag/perf_config.h"
#include "spdiag/shm_layout.h"
#include "test_helpers/spdiag_test_fixtures.h"

class PerfPointTest : public ::testing::Test {
protected:
    void SetUp() override {
        spdiag_test::InitPerfManagerForTest("/spdiag_test_point", 4, SpDiag::RunMode::DEBUG, 60000);
        SPDIAG_SKIP_IF_NO_SHM();
    }

    void TearDown() override {
        SpDiag::PerfManager::Instance().Shutdown();
    }
};

static constexpr uint32_t kStatCount = static_cast<uint32_t>(SpDiag::PerfKeyDefined::PERF_KEY_DEFINED_COUNT);

TEST_F(PerfPointTest, NormalEnd) {
    {
        SpDiag::PerfPoint pt(SpDiag::PerfKeyDefined::COMPUTE_MATMUL, SpDiag::PerfLevel::SUB_SYSTEM);
        pt.Start();
        pt.End(0);
    }
    auto& shard = SpDiag::PerfManager::Instance().GetShard();
    bool found = false;
    for (uint32_t i = 0; i < kStatCount; ++i) {
        if (shard.stats[i].backGoodCount > 0) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(PerfPointTest, BadReturnCode) {
    {
        SpDiag::PerfPoint pt(SpDiag::PerfKeyDefined::COMPUTE_PRIME, SpDiag::PerfLevel::SUB_SYSTEM);
        pt.Start();
        pt.End(1);
    }
    auto& shard = SpDiag::PerfManager::Instance().GetShard();
    bool found = false;
    for (uint32_t i = 0; i < kStatCount; ++i) {
        if (shard.stats[i].backBadCount > 0) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(PerfPointTest, AbandonOnDestruct) {
    {
        SpDiag::PerfPoint pt(SpDiag::PerfKeyDefined::IO_IOTASK, SpDiag::PerfLevel::SUB_SYSTEM);
        pt.Start();
    }
    auto& shard = SpDiag::PerfManager::Instance().GetShard();
    bool found = false;
    for (uint32_t i = 0; i < kStatCount; ++i) {
        if (shard.stats[i].backNotCount > 0) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(PerfPointTest, LevelFiltering) {
    {
        SpDiag::PerfPoint pt(SpDiag::PerfKeyDefined::PROCESS_WORK, static_cast<SpDiag::PerfLevel>(99));
        pt.Start();
        pt.End(0);
    }
    auto& shard = SpDiag::PerfManager::Instance().GetShard();
    for (uint32_t i = 0; i < kStatCount; ++i) {
        EXPECT_EQ(shard.stats[i].backGoodCount, 0u);
        EXPECT_EQ(shard.stats[i].backBadCount, 0u);
        EXPECT_EQ(shard.stats[i].tickCount, 0u);
    }
}

TEST_F(PerfPointTest, ExplicitAbandon) {
    {
        SpDiag::PerfPoint pt(SpDiag::PerfKeyDefined::PIPELINE_STAGE1, SpDiag::PerfLevel::SUB_SYSTEM);
        pt.Start();
        pt.Abandon();
    }
    auto& shard = SpDiag::PerfManager::Instance().GetShard();
    bool found = false;
    for (uint32_t i = 0; i < kStatCount; ++i) {
        if (shard.stats[i].backNotCount > 0) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(PerfPointNoShmTest, DisabledWhenSharedMemoryMissing) {
    SpDiag::PerfManager::Instance().Shutdown();
    SpDiag::PerfConfig config;
    config.shmName = "/spdiag_shm_definitely_missing_for_perf_point_test";
    config.mode = SpDiag::RunMode::DEBUG;
    config.levelGate = 4;
    SpDiag::PerfManager::Instance().Init(config);
    ASSERT_FALSE(SpDiag::PerfManager::Instance().IsInitialized());

    {
        SpDiag::PerfPoint pt(SpDiag::PerfKeyDefined::COMPUTE_MATMUL,
                             SpDiag::PerfLevel::SUB_SYSTEM);
        pt.Start();
        pt.End(0);
    }

    {
        SpDiag::PerfPoint pt(SpDiag::PerfKeyDefined::COMPUTE_PRIME,
                             SpDiag::PerfLevel::SUB_SYSTEM);
        pt.Start();
        pt.Abandon();
    }

    EXPECT_FALSE(SpDiag::PerfManager::Instance().IsInitialized());
}
