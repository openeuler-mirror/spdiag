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

#ifdef SPDIAG_ENABLE_OB_CACHE

#include "cachestat.h"

#include <chrono>
#include <cstring>

#include "spdiag/log_macros.h"

namespace SpDiag {

SpDiagCachestatPlugin::SpDiagCachestatPlugin() = default;

SpDiagCachestatPlugin::~SpDiagCachestatPlugin() {
    Stop();
}

SpDiagPluginMeta SpDiagCachestatPlugin::Metadata() const {
    SpDiagPluginMeta meta;
    meta.name = "cachestat";
    meta.version = "1.0.0";
    meta.featureNames = {"FEAT_OB_CACHE"};
    meta.minKernelVersion = "5.4";
    meta.capabilities = {"CAP_PERFMON"};
    meta.resourceReq.mapMemoryKb = 0;
    meta.resourceReq.maxProbes = 0;
    meta.resourceReq.maxChannels = 1;
    meta.featureFlags = FEAT_OB_CACHE;
    return meta;
}

bool SpDiagCachestatPlugin::Init(const SpDiagPluginConfig& config) {
    if (config.ConfigTypeId() == nullptr || std::strcmp(config.ConfigTypeId(), "cachestat") != 0) {
        return false;
    }
    std::string err;
    if (!ObEnv::CheckPerfEventParanoid(&err)) {
        health_.healthy = false;
        health_.message = err;
        return false;
    }
    config_ = static_cast<const CachestatConfig&>(config);
    health_.healthy = true;
    health_.message = "cachestat initialized";
    health_.lastUpdatedNs = Platform::GetTimestampNs();
    stopRequested_.store(false);
    return true;
}

bool SpDiagCachestatPlugin::Start() {
    stopRequested_.store(false);
    if (!sampler_.Open(config_.cacheLevels, config_.targetPid)) {
        health_.healthy = false;
        health_.message = "CacheHitRateSampler open failed";
        SPDIAG_LOG_ERROR(LogModule::PLATFORM, "CacheHitRateSampler open failed for pid %d", config_.targetPid);
        return false;
    }
    collectThread_ = std::make_unique<std::thread>(&SpDiagCachestatPlugin::CollectLoop, this);
    health_.healthy = true;
    health_.message = "cachestat running";
    health_.lastUpdatedNs = Platform::GetTimestampNs();
    return true;
}

void SpDiagCachestatPlugin::CollectLoop() {
    while (!stopRequested_.load()) {
        if (config_.dataSink) {
            sampler_.Collect(config_.dataSink);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.pollIntervalMs));
    }
}

bool SpDiagCachestatPlugin::Stop() {
    stopRequested_.store(true);
    if (collectThread_ && collectThread_->joinable()) {
        collectThread_->join();
    }
    collectThread_.reset();
    sampler_.Close();
    health_.healthy = true;
    health_.message = "cachestat stopped";
    health_.lastUpdatedNs = Platform::GetTimestampNs();
    return true;
}

bool SpDiagCachestatPlugin::Destroy() {
    Stop();
    health_.healthy = true;
    health_.message = "cachestat destroyed";
    health_.lastUpdatedNs = Platform::GetTimestampNs();
    return true;
}

bool SpDiagCachestatPlugin::Reconfig(const SpDiagPluginConfig& config) {
    if (config.ConfigTypeId() == nullptr || std::strcmp(config.ConfigTypeId(), "cachestat") != 0) {
        return false;
    }
    const auto& cfg = static_cast<const CachestatConfig&>(config);
    config_.pollIntervalMs = cfg.pollIntervalMs;
    config_.cacheLevels = cfg.cacheLevels;
    health_.lastUpdatedNs = Platform::GetTimestampNs();
    return true;
}

bool SpDiagCachestatPlugin::Query(const SpDiagQueryFilter& filter, SpDiagResultSet& result) {
    (void)filter;
    result.rows.clear();
    return true;
}

SpDiagHealthInfo SpDiagCachestatPlugin::Health() const {
    return health_;
}

}  // namespace SpDiag

#endif
