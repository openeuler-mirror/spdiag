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

#include "src/runtime/processor.h"

TEST(ProcessorTest, ProbeAndChannelLifecycle) {
    SpDiag::SpDiagProcessor processor;

    SpDiag::ProbeDesc probe;
    probe.type = SpDiag::ProbeType::SOFTWARE;
    probe.symbolName = "DummyProbe";
    auto handle = processor.RegisterProbe(probe);
    EXPECT_GT(handle, 0u);
    EXPECT_TRUE(processor.UnregisterProbe(handle));

    uint32_t channelId = processor.CreateChannel("test_plugin");
    EXPECT_GT(channelId, 0u);

    SpDiag::ProcessorData data;
    data.key = "k";
    data.value = "v";
    EXPECT_TRUE(processor.SubmitData(channelId, data));

    auto output = processor.GetOutput();
    ASSERT_EQ(output.size(), 1u);
    EXPECT_EQ(output[0].key, "k");
    EXPECT_EQ(output[0].value, "v");

    EXPECT_TRUE(processor.DestroyChannel(channelId));
}

TEST(ProcessorTest, DestroyChannelRejectsUnknownId) {
    SpDiag::SpDiagProcessor processor;
    // Unknown ids (including 0, never handed out) must be rejected safely.
    EXPECT_FALSE(processor.DestroyChannel(9999u));
    EXPECT_FALSE(processor.DestroyChannel(0u));
}

TEST(ProcessorTest, SubmitDataRejectsUnknownChannel) {
    SpDiag::SpDiagProcessor processor;
    SpDiag::ProcessorData data;
    data.key = "k";
    data.value = "v";
    EXPECT_FALSE(processor.SubmitData(9999u, data));
}

TEST(ProcessorTest, GetOutputIsEmptyWithoutSubmittedData) {
    SpDiag::SpDiagProcessor processor;
    uint32_t channelId = processor.CreateChannel("idle_plugin");
    EXPECT_GT(channelId, 0u);

    auto output = processor.GetOutput();
    EXPECT_TRUE(output.empty());

    EXPECT_TRUE(processor.DestroyChannel(channelId));
}

TEST(ProcessorTest, CreateChannelReturnsDistinctIds) {
    SpDiag::SpDiagProcessor processor;
    uint32_t id1 = processor.CreateChannel("plugin_a");
    uint32_t id2 = processor.CreateChannel("plugin_b");
    EXPECT_GT(id1, 0u);
    EXPECT_GT(id2, 0u);
    EXPECT_NE(id1, id2);

    EXPECT_TRUE(processor.DestroyChannel(id1));
    EXPECT_TRUE(processor.DestroyChannel(id2));
}

TEST(ProcessorTest, SubmitDataFailsAfterChannelDestroyed) {
    SpDiag::SpDiagProcessor processor;
    uint32_t channelId = processor.CreateChannel("ephemeral");
    EXPECT_GT(channelId, 0u);
    ASSERT_TRUE(processor.DestroyChannel(channelId));

    SpDiag::ProcessorData data;
    data.key = "k";
    data.value = "v";
    EXPECT_FALSE(processor.SubmitData(channelId, data));
}
