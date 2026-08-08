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
#include "spdiag/perf_key_defined.h"

TEST(PerfKeyDefinedTest, EnumValues) {
    EXPECT_EQ(static_cast<uint32_t>(SpDiag::PerfKeyDefined::NONE), 0u);
    EXPECT_EQ(static_cast<uint32_t>(SpDiag::PerfKeyDefined::COMPUTE_MATMUL), 1u);
    EXPECT_EQ(static_cast<uint32_t>(SpDiag::PerfKeyDefined::COMPUTE_PRIME), 2u);
    EXPECT_EQ(static_cast<uint32_t>(SpDiag::PerfKeyDefined::IO_IOTASK), 21u);
    EXPECT_EQ(static_cast<uint32_t>(SpDiag::PerfKeyDefined::PROCESS_WORK), 41u);
    EXPECT_EQ(static_cast<uint32_t>(SpDiag::PerfKeyDefined::PIPELINE_STAGE1), 61u);
    EXPECT_EQ(static_cast<uint32_t>(SpDiag::PerfKeyDefined::PIPELINE_STAGE2), 62u);
    EXPECT_EQ(static_cast<uint32_t>(SpDiag::PerfKeyDefined::PIPELINE_STAGE3), 63u);
    EXPECT_EQ(static_cast<uint32_t>(SpDiag::PerfKeyDefined::SYSTEM_SCHEDULER), 81u);
    EXPECT_EQ(static_cast<uint32_t>(SpDiag::PerfKeyDefined::MEMORY_ALLOC), 101u);
}

TEST(PerfKeyDefinedTest, SentinelValue) {
    constexpr uint32_t count = static_cast<uint32_t>(SpDiag::PerfKeyDefined::PERF_KEY_DEFINED_COUNT);
    EXPECT_EQ(count, 121u);
}

TEST(PerfKeyDefinedTest, NameArraysSize) {
    constexpr uint32_t count = static_cast<uint32_t>(SpDiag::PerfKeyDefined::PERF_KEY_DEFINED_COUNT);
    for (uint32_t i = 0; i < count - 1; ++i) {
        EXPECT_STRNE(SpDiag::perfKeyDefinedModules[i], "")
            << "Module name at index " << i << " should not be empty";
        EXPECT_STRNE(SpDiag::perfKeyDefinedPointNames[i], "")
            << "Point name at index " << i << " should not be empty";
    }
}

TEST(PerfKeyDefinedTest, SpecificNameValues) {
    EXPECT_STREQ(SpDiag::perfKeyDefinedModules[0], "Compute");
    EXPECT_STREQ(SpDiag::perfKeyDefinedPointNames[0], "MatMul");

    EXPECT_STREQ(SpDiag::perfKeyDefinedModules[1], "Compute");
    EXPECT_STREQ(SpDiag::perfKeyDefinedPointNames[1], "Prime");

    EXPECT_STREQ(SpDiag::perfKeyDefinedModules[20], "IO");
    EXPECT_STREQ(SpDiag::perfKeyDefinedPointNames[20], "IOTask");

    EXPECT_STREQ(SpDiag::perfKeyDefinedModules[40], "Process");
    EXPECT_STREQ(SpDiag::perfKeyDefinedPointNames[40], "Work");

    EXPECT_STREQ(SpDiag::perfKeyDefinedModules[60], "Pipeline");
    EXPECT_STREQ(SpDiag::perfKeyDefinedPointNames[60], "Stage1");

    EXPECT_STREQ(SpDiag::perfKeyDefinedModules[61], "Pipeline");
    EXPECT_STREQ(SpDiag::perfKeyDefinedPointNames[61], "Stage2");

    EXPECT_STREQ(SpDiag::perfKeyDefinedModules[62], "Pipeline");
    EXPECT_STREQ(SpDiag::perfKeyDefinedPointNames[62], "Stage3");

    EXPECT_STREQ(SpDiag::perfKeyDefinedModules[80], "System");
    EXPECT_STREQ(SpDiag::perfKeyDefinedPointNames[80], "Scheduler");

    EXPECT_STREQ(SpDiag::perfKeyDefinedModules[100], "Memory");
    EXPECT_STREQ(SpDiag::perfKeyDefinedPointNames[100], "Alloc");
}
