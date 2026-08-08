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

#include "src/runtime/plugin_interface.h"
#include "src/runtime/plugin_types.h"

namespace {

struct DummyConfig : SpDiag::SpDiagPluginConfig {
    const char* ConfigTypeId() const override { return "dummy"; }
};

class DummyPlugin final : public SpDiag::SpDiagPluginInterface {
public:
    SpDiag::SpDiagPluginMeta Metadata() const override {
        SpDiag::SpDiagPluginMeta meta;
        meta.name = "dummy";
        meta.version = "1.0.0";
        meta.featureFlags = SpDiag::FEAT_CUSTOM_UPROBE;
        return meta;
    }

    bool Init(const SpDiag::SpDiagPluginConfig& config) override {
        (void)config;
        initialized_ = true;
        return true;
    }

    bool Start() override {
        if (!initialized_) {
            return false;
        }
        running_ = true;
        return true;
    }

    bool Stop() override {
        running_ = false;
        return true;
    }

    bool Destroy() override {
        initialized_ = false;
        return true;
    }

    bool Reconfig(const SpDiag::SpDiagPluginConfig& config) override {
        (void)config;
        return true;
    }

    bool Query(const SpDiag::SpDiagQueryFilter& filter, SpDiag::SpDiagResultSet& result) override {
        (void)filter;
        SpDiag::SpDiagResultRow row;
        row.key = "state";
        row.value = running_ ? "running" : "idle";
        result.rows.push_back(row);
        return true;
    }

    SpDiag::SpDiagHealthInfo Health() const override {
        SpDiag::SpDiagHealthInfo info;
        info.healthy = true;
        info.message = running_ ? "running" : "idle";
        return info;
    }

private:
    bool initialized_ = false;
    bool running_ = false;
};

}

TEST(PluginInterfaceTest, MetadataAndLifecycleWork) {
    DummyPlugin plugin;
    auto meta = plugin.Metadata();
    EXPECT_EQ(meta.name, "dummy");
    EXPECT_TRUE(SpDiag::HasFeature(meta.featureFlags, SpDiag::FEAT_CUSTOM_UPROBE));

    DummyConfig config;
    EXPECT_TRUE(plugin.Init(config));
    EXPECT_TRUE(plugin.Start());

    SpDiag::SpDiagResultSet result;
    SpDiag::SpDiagQueryFilter filter;
    EXPECT_TRUE(plugin.Query(filter, result));
    ASSERT_EQ(result.rows.size(), 1u);
    EXPECT_EQ(result.rows[0].value, "running");

    EXPECT_TRUE(plugin.Stop());
    EXPECT_TRUE(plugin.Destroy());
}

TEST(PluginInterfaceTest, ReconfigAcceptsNewConfig) {
    DummyPlugin plugin;
    DummyConfig initConfig;
    ASSERT_TRUE(plugin.Init(initConfig));

    DummyConfig newConfig;
    EXPECT_TRUE(plugin.Reconfig(newConfig));

    EXPECT_TRUE(plugin.Destroy());
}

TEST(PluginInterfaceTest, QueryWithNeutralFilterIsWellFormed) {
    DummyPlugin plugin;
    DummyConfig config;
    ASSERT_TRUE(plugin.Init(config));
    ASSERT_TRUE(plugin.Start());

    SpDiag::SpDiagResultSet result;
    SpDiag::SpDiagQueryFilter filter;  // empty/neutral filter
    EXPECT_TRUE(plugin.Query(filter, result));
    EXPECT_FALSE(result.rows.empty());
    for (const auto& row : result.rows) {
        EXPECT_FALSE(row.key.empty());
    }

    EXPECT_TRUE(plugin.Stop());
    EXPECT_TRUE(plugin.Destroy());
}

TEST(PluginInterfaceTest, HealthReturnsSaneInfo) {
    DummyPlugin plugin;

    auto info = plugin.Health();
    EXPECT_TRUE(info.healthy);
    EXPECT_FALSE(info.message.empty());
}

TEST(PluginInterfaceTest, StartAndStopAreIdempotent) {
    DummyPlugin plugin;
    DummyConfig config;
    ASSERT_TRUE(plugin.Init(config));

    // DummyPlugin permits repeated Start()/Stop() without state guarding.
    EXPECT_TRUE(plugin.Start());
    EXPECT_TRUE(plugin.Start());

    EXPECT_TRUE(plugin.Stop());
    EXPECT_TRUE(plugin.Stop());

    EXPECT_TRUE(plugin.Destroy());
}

TEST(PluginInterfaceTest, DestroyIsIdempotentAndGuardsSubsequentStart) {
    DummyPlugin plugin;
    DummyConfig config;
    ASSERT_TRUE(plugin.Init(config));

    EXPECT_TRUE(plugin.Destroy());
    // A subsequent Destroy() is safe and returns true per the DummyPlugin contract.
    EXPECT_TRUE(plugin.Destroy());
    // After Destroy(), initialized_ is cleared, so Start() must be rejected.
    EXPECT_FALSE(plugin.Start());
}
