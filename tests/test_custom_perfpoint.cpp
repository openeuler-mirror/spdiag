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

#include "src/runtime/plugins/custom_perfpoint/custom_perfpoint.h"
#include "src/runtime/plugin_types.h"

TEST(CustomPerfpointTest, MetadataContract) {
    SpDiag::CustomPerfpoint plugin;
    auto meta = plugin.Metadata();

    EXPECT_EQ(meta.name, "custom_perfpoint");
    EXPECT_EQ(meta.version, "1.0.0");
    EXPECT_TRUE(SpDiag::HasFeature(meta.featureFlags, SpDiag::FEAT_CUSTOM_UPROBE));
    EXPECT_TRUE(SpDiag::HasFeature(meta.featureFlags, SpDiag::FEAT_CUSTOM_TRACEPOINT));
}

TEST(CustomPerfpointTest, ConfigTypeId) {
    SpDiag::CustomPerfpointConfig config;
    EXPECT_STREQ(config.ConfigTypeId(), "custom_perfpoint");
}

TEST(CustomPerfpointTest, ConfigDefaults) {
    SpDiag::CustomPerfpointConfig config;
    EXPECT_EQ(config.shmName, "/spdiag_shm_default");
    EXPECT_EQ(config.dataSink, nullptr);
    EXPECT_FALSE(config.watchMode);
    EXPECT_EQ(config.watchIntervalMs, 2000u);
    EXPECT_FALSE(config.strictSnapshot);
}

TEST(CustomPerfpointTest, InitRejectsBadConfig) {
    SpDiag::CustomPerfpoint plugin;
    struct BadConfig : SpDiag::SpDiagPluginConfig {
        const char* ConfigTypeId() const override { return "bad"; }
    } badConfig;
    bool result = plugin.Init(badConfig);
    EXPECT_FALSE(result);
}

TEST(CustomPerfpointTest, InitAcceptsValidConfig) {
    SpDiag::CustomPerfpoint plugin;
    SpDiag::CustomPerfpointConfig config;
    config.shmName = "/spdiag_shm_nonexistent";
    bool result = plugin.Init(config);
    EXPECT_TRUE(result);
}

TEST(CustomPerfpointTest, StopBeforeStart) {
    SpDiag::CustomPerfpoint plugin;
    SpDiag::CustomPerfpointConfig config;
    config.shmName = "/spdiag_shm_nonexistent";
    plugin.Init(config);
    bool result = plugin.Stop();
    EXPECT_TRUE(result);
}

TEST(CustomPerfpointTest, DestroyCleansUp) {
    SpDiag::CustomPerfpoint plugin;
    SpDiag::CustomPerfpointConfig config;
    config.shmName = "/spdiag_shm_nonexistent";
    plugin.Init(config);
    bool result = plugin.Destroy();
    EXPECT_TRUE(result);
}

TEST(CustomPerfpointTest, HealthAfterInit) {
    SpDiag::CustomPerfpoint plugin;
    SpDiag::CustomPerfpointConfig config;
    config.shmName = "/spdiag_shm_nonexistent";
    plugin.Init(config);
    auto health = plugin.Health();
    EXPECT_TRUE(health.healthy);
    EXPECT_EQ(health.message, "custom_perfpoint initialized");
}

TEST(CustomPerfpointTest, StartFailsWithoutShm) {
    SpDiag::CustomPerfpoint plugin;
    SpDiag::CustomPerfpointConfig config;
    config.shmName = "/spdiag_shm_definitely_nonexistent_12345";
    plugin.Init(config);
    bool result = plugin.Start();
    EXPECT_FALSE(result);
}
