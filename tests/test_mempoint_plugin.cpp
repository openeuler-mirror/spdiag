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

#ifdef SPDIAG_ENABLE_MEMPOINT
#include "src/runtime/plugins/mempoint/mempoint.h"
#endif

#include "src/runtime/plugin_types.h"

#if defined(SPDIAG_ENABLE_MEMPOINT)

TEST(MempointPluginTest, Metadata) {
    SpDiag::SpDiagMempointPlugin plugin;
    auto meta = plugin.Metadata();
    EXPECT_EQ(meta.name, "mempoint");
    EXPECT_TRUE(SpDiag::HasFeature(meta.featureFlags, SpDiag::FEAT_MEMPOINT));
    EXPECT_FALSE(SpDiag::HasFeature(meta.featureFlags, SpDiag::FEAT_OB_CACHE));
}

TEST(MempointPluginTest, ConfigTypeId) {
    SpDiag::MempointConfig config;
    EXPECT_STREQ(config.ConfigTypeId(), "mempoint");
}

TEST(MempointPluginTest, ConfigDefaults) {
    SpDiag::MempointConfig config;
    EXPECT_EQ(config.targetPid, -1);
    EXPECT_TRUE(config.binaryPath.empty());
    EXPECT_EQ(config.pollIntervalMs, 1000u);
    EXPECT_EQ(config.bpfContext, nullptr);
    EXPECT_EQ(config.dataSink, nullptr);
}

TEST(MempointPluginTest, InitRejectsBadConfig) {
    SpDiag::SpDiagMempointPlugin plugin;
    struct BadConfig : SpDiag::SpDiagPluginConfig {
        const char* ConfigTypeId() const override { return "bad"; }
    } badConfig;
    EXPECT_FALSE(plugin.Init(badConfig));
}

// Start() is deliberately NOT exercised here: it unconditionally dereferences
// config_.bpfContext (->AcquireMempointUsdts()), so with the default config
// (bpfContext == nullptr) it would crash rather than return false. A real
// ObBpfContext cannot be constructed without a full BPF load, which is out of
// scope for these no-attach lifecycle tests. The cases below cover the safe
// paths that do not require a BPF target.

TEST(MempointPluginTest, StopIsIdempotentWithoutStart) {
    SpDiag::SpDiagMempointPlugin plugin;
    // Stop() must be safe on a never-Started plugin (the destructor relies on
    // this) and must remain safe when called a second time. It guards the null
    // bpfContext and PointMemProfiler::Detach() is a no-op when unattached.
    EXPECT_TRUE(plugin.Stop());
    EXPECT_TRUE(plugin.Stop());
}

TEST(MempointPluginTest, DestroyIsIdempotentWithoutStart) {
    SpDiag::SpDiagMempointPlugin plugin;
    EXPECT_TRUE(plugin.Destroy());
    EXPECT_TRUE(plugin.Destroy());
}

TEST(MempointPluginTest, ReconfigRejectsBadConfig) {
    SpDiag::SpDiagMempointPlugin plugin;
    struct BadConfig : SpDiag::SpDiagPluginConfig {
        const char* ConfigTypeId() const override { return "bad"; }
    } badConfig;
    EXPECT_FALSE(plugin.Reconfig(badConfig));
}

TEST(MempointPluginTest, QuerySucceedsAndClearsRows) {
    SpDiag::SpDiagMempointPlugin plugin;
    SpDiag::SpDiagQueryFilter filter;
    SpDiag::SpDiagResultSet result;
    result.rows.push_back({"stale", "data"});
    EXPECT_TRUE(plugin.Query(filter, result));
    EXPECT_TRUE(result.rows.empty());
}

#endif
