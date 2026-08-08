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

#ifdef SPDIAG_ENABLE_OB_CACHE
#include "src/runtime/plugins/cachestat/cachestat.h"
#endif

#include "src/runtime/plugin_types.h"

#if defined(SPDIAG_ENABLE_OB_CACHE)

TEST(CachestatPluginTest, Metadata) {
    SpDiag::SpDiagCachestatPlugin plugin;
    auto meta = plugin.Metadata();
    EXPECT_EQ(meta.name, "cachestat");
    EXPECT_TRUE(SpDiag::HasFeature(meta.featureFlags, SpDiag::FEAT_OB_CACHE));
    EXPECT_FALSE(SpDiag::HasFeature(meta.featureFlags, SpDiag::FEAT_OB_MEMORY));
}

TEST(CachestatPluginTest, ConfigTypeId) {
    SpDiag::CachestatConfig config;
    EXPECT_STREQ(config.ConfigTypeId(), "cachestat");
}

TEST(CachestatPluginTest, ConfigDefaults) {
    SpDiag::CachestatConfig config;
    EXPECT_EQ(config.targetPid, -1);
    EXPECT_EQ(config.pollIntervalMs, 1000u);
    EXPECT_EQ(config.cacheLevels.size(), 3u);
    EXPECT_EQ(config.dataSink, nullptr);
}

TEST(CachestatPluginTest, InitRejectsBadConfig) {
    SpDiag::SpDiagCachestatPlugin plugin;
    struct BadConfig : SpDiag::SpDiagPluginConfig {
        const char* ConfigTypeId() const override { return "bad"; }
    } badConfig;
    EXPECT_FALSE(plugin.Init(badConfig));
}

// Unlike memstat/mempoint, cachestat does not consume an ObBpfContext and its
// Start() does not dereference anything. With the default config (targetPid=-1)
// CacheHitRateSampler::Open resolves /proc/-1/task which does not exist, so
// RefreshThreads() returns immediately, no perf_event_open is attempted, and
// Open() -> Start() deterministically returns false without any real attach.
TEST(CachestatPluginTest, StartReturnsFalseWithoutTargetPid) {
    SpDiag::SpDiagCachestatPlugin plugin;
    EXPECT_FALSE(plugin.Start());
    // The plugin must remain in a consistent state: Stop() still returns true
    // and joins no thread (none was created because Start() failed).
    EXPECT_TRUE(plugin.Stop());
}

TEST(CachestatPluginTest, StopIsIdempotentWithoutStart) {
    SpDiag::SpDiagCachestatPlugin plugin;
    EXPECT_TRUE(plugin.Stop());
    EXPECT_TRUE(plugin.Stop());
}

TEST(CachestatPluginTest, DestroyIsIdempotentWithoutStart) {
    SpDiag::SpDiagCachestatPlugin plugin;
    EXPECT_TRUE(plugin.Destroy());
    EXPECT_TRUE(plugin.Destroy());
}

TEST(CachestatPluginTest, ReconfigRejectsBadConfig) {
    SpDiag::SpDiagCachestatPlugin plugin;
    struct BadConfig : SpDiag::SpDiagPluginConfig {
        const char* ConfigTypeId() const override { return "bad"; }
    } badConfig;
    EXPECT_FALSE(plugin.Reconfig(badConfig));
}

TEST(CachestatPluginTest, QuerySucceedsAndClearsRows) {
    SpDiag::SpDiagCachestatPlugin plugin;
    SpDiag::SpDiagQueryFilter filter;
    SpDiag::SpDiagResultSet result;
    result.rows.push_back({"stale", "data"});
    EXPECT_TRUE(plugin.Query(filter, result));
    EXPECT_TRUE(result.rows.empty());
}

#endif
