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

#pragma once

// Cachestat plugin: wraps CacheHitRateSampler (perf_event_open based) as an
// independent SpDiagPluginInterface. Unlike the old SpDiagObPoint, this plugin
// owns its lifecycle, health and collect cadence independently. It does not
// touch BPF, so it has no ObBpfContext dependency.

#ifdef SPDIAG_ENABLE_OB_CACHE

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "cache_sampler.h"
#include "data_sink.h"
#include "ob_env.h"
#include "plugin_interface.h"
#include "sdk/platform.h"

namespace SpDiag {

struct CachestatConfig : SpDiagPluginConfig {
    const char* ConfigTypeId() const override { return "cachestat"; }

    int32_t targetPid = -1;
    std::vector<std::string> cacheLevels = {"L1-dcache", "LLC", "dTLB"};
    uint32_t pollIntervalMs = 1000;
    DataSink* dataSink = nullptr;
};

class SpDiagCachestatPlugin final : public SpDiagPluginInterface {
public:
    SpDiagCachestatPlugin();
    ~SpDiagCachestatPlugin() override;

    SpDiagPluginMeta Metadata() const override;
    bool Init(const SpDiagPluginConfig& config) override;
    bool Start() override;
    bool Stop() override;
    bool Destroy() override;
    bool Reconfig(const SpDiagPluginConfig& config) override;
    bool Query(const SpDiagQueryFilter& filter, SpDiagResultSet& result) override;
    SpDiagHealthInfo Health() const override;

private:
    void CollectLoop();

    CachestatConfig config_;
    SpDiagHealthInfo health_;
    CacheHitRateSampler sampler_;
    std::atomic<bool> stopRequested_{false};
    std::unique_ptr<std::thread> collectThread_;
};

}  // namespace SpDiag

#endif
