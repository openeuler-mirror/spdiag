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
#include "spdiag/perf_manager.h"
#include "spdiag/perf_key_defined.h"
#include "test_helpers/spdiag_test_fixtures.h"

class PerfManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        spdiag_test::InitPerfManagerForTest("/spdiag_test_mgr", 3, SpDiag::RunMode::DEBUG, 60000);
        SPDIAG_SKIP_IF_NO_SHM();
    }

    void TearDown() override {
        SpDiag::PerfManager::Instance().Shutdown();
    }
};

TEST_F(PerfManagerTest, Singleton) {
    auto& a = SpDiag::PerfManager::Instance();
    auto& b = SpDiag::PerfManager::Instance();
    EXPECT_EQ(&a, &b);
}

TEST_F(PerfManagerTest, FastResolveSlot) {
    uint32_t slot1 = SpDiag::PerfManager::Instance().FastResolveSlot(
        static_cast<uint32_t>(SpDiag::PerfKeyDefined::COMPUTE_MATMUL));
    uint32_t slot2 = SpDiag::PerfManager::Instance().FastResolveSlot(
        static_cast<uint32_t>(SpDiag::PerfKeyDefined::COMPUTE_MATMUL));
    EXPECT_EQ(slot1, slot2);

    uint32_t slot3 = SpDiag::PerfManager::Instance().FastResolveSlot(
        static_cast<uint32_t>(SpDiag::PerfKeyDefined::COMPUTE_PRIME));
    EXPECT_NE(slot1, slot3);
}

TEST_F(PerfManagerTest, FastResolveSlotInvalid) {
    uint32_t slot = SpDiag::PerfManager::Instance().FastResolveSlot(UINT32_MAX);
    EXPECT_EQ(slot, UINT32_MAX);
}

TEST_F(PerfManagerTest, LevelGateValue) {
    EXPECT_EQ(SpDiag::PerfManager::Instance().LevelGate(), 3u);
}

TEST_F(PerfManagerTest, LevelGateFiltering) {
    uint8_t gate = SpDiag::PerfManager::Instance().LevelGate();
    EXPECT_TRUE(static_cast<uint8_t>(SpDiag::PerfLevel::SUB_SYSTEM) <= gate);
    EXPECT_TRUE(static_cast<uint8_t>(SpDiag::PerfLevel::KEY_MODULE) <= gate);
    EXPECT_TRUE(static_cast<uint8_t>(SpDiag::PerfLevel::MODULE) <= gate);
    EXPECT_FALSE(static_cast<uint8_t>(SpDiag::PerfLevel::DEBUG) <= gate);
}

TEST(PerfKeyDefinedTest, O1IndexCorrectness) {
    constexpr uint32_t count = static_cast<uint32_t>(SpDiag::PerfKeyDefined::PERF_KEY_DEFINED_COUNT);
    EXPECT_EQ(count, 121u);

    EXPECT_STREQ(SpDiag::perfKeyDefinedModules[0], "Compute");
    EXPECT_STREQ(SpDiag::perfKeyDefinedPointNames[0], "MatMul");

    EXPECT_STREQ(SpDiag::perfKeyDefinedModules[1], "Compute");
    EXPECT_STREQ(SpDiag::perfKeyDefinedPointNames[1], "Prime");

    EXPECT_STREQ(SpDiag::perfKeyDefinedModules[20], "IO");
    EXPECT_STREQ(SpDiag::perfKeyDefinedPointNames[20], "IOTask");

    for (uint32_t i = 1; i < count; ++i) {
        uint32_t arrIdx = i - 1;
        EXPECT_STRNE(SpDiag::perfKeyDefinedModules[arrIdx], "");
        EXPECT_STRNE(SpDiag::perfKeyDefinedPointNames[arrIdx], "");
    }
}
