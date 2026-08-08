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

#ifdef SPDIAG_ENABLE_MEMPOINT

#include "mempoint.h"

#include <chrono>
#include <cstring>

#include "spdiag/log_macros.h"

namespace SpDiag {

SpDiagMempointPlugin::SpDiagMempointPlugin() = default;

SpDiagMempointPlugin::~SpDiagMempointPlugin() {
    Stop();
}

SpDiagPluginMeta SpDiagMempointPlugin::Metadata() const {
    SpDiagPluginMeta meta;
    meta.name = "mempoint";
    meta.version = "1.0.0";
    meta.featureNames = {"FEAT_MEMPOINT"};
    meta.minKernelVersion = "5.4";
    meta.capabilities = {"CAP_SYS_ADMIN", "CAP_BPF"};
    meta.resourceReq.mapMemoryKb = 512;
    meta.resourceReq.maxProbes = 16;
    meta.resourceReq.maxChannels = 1;
    meta.featureFlags = FEAT_MEMPOINT;
    return meta;
}

bool SpDiagMempointPlugin::Init(const SpDiagPluginConfig& config) {
    if (config.ConfigTypeId() == nullptr || std::strcmp(config.ConfigTypeId(), "mempoint") != 0) {
        return false;
    }
    std::string err;
    if (!ObEnv::CheckAll(&err)) {
        health_.healthy = false;
        health_.message = err;
        return false;
    }
    config_ = static_cast<const MempointConfig&>(config);
    if (!config_.bpfContext) {
        health_.healthy = false;
        health_.message = "MempointConfig.bpfContext is null";
        return false;
    }
    health_.healthy = true;
    health_.message = "mempoint initialized";
    health_.lastUpdatedNs = Platform::GetTimestampNs();
    stopRequested_.store(false);
    return true;
}

bool SpDiagMempointPlugin::Start() {
    stopRequested_.store(false);
    if (!config_.bpfContext->AcquireMempointUsdts()) {
        health_.healthy = false;
        health_.message = "ObBpfContext AcquireMempointUsdts failed";
        SPDIAG_LOG_ERROR(LogModule::PLATFORM, "mempoint AcquireMempointUsdts failed for pid %d", config_.targetPid);
        return false;
    }
    struct bpf_object* obj = config_.bpfContext->GetBpfObject();
    if (!obj || !profiler_.Attach(obj, config_.targetPid, config_.binaryPath)) {
        config_.bpfContext->ReleaseMempointUsdts();
        health_.healthy = false;
        health_.message = "PointMemProfiler attach failed";
        SPDIAG_LOG_ERROR(LogModule::PLATFORM, "PointMemProfiler attach failed for pid %d", config_.targetPid);
        return false;
    }
    collectThread_ = std::make_unique<std::thread>(&SpDiagMempointPlugin::CollectLoop, this);
    health_.healthy = true;
    health_.message = "mempoint running";
    health_.lastUpdatedNs = Platform::GetTimestampNs();
    return true;
}

void SpDiagMempointPlugin::CollectLoop() {
    while (!stopRequested_.load()) {
        if (config_.dataSink) {
            profiler_.Collect(config_.dataSink);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.pollIntervalMs));
    }
}

bool SpDiagMempointPlugin::Stop() {
    stopRequested_.store(true);
    if (collectThread_ && collectThread_->joinable()) {
        collectThread_->join();
    }
    collectThread_.reset();
    profiler_.Detach();
    if (config_.bpfContext) {
        config_.bpfContext->ReleaseMempointUsdts();
    }
    health_.healthy = true;
    health_.message = "mempoint stopped";
    health_.lastUpdatedNs = Platform::GetTimestampNs();
    return true;
}

bool SpDiagMempointPlugin::Destroy() {
    Stop();
    health_.healthy = true;
    health_.message = "mempoint destroyed";
    health_.lastUpdatedNs = Platform::GetTimestampNs();
    return true;
}

bool SpDiagMempointPlugin::Reconfig(const SpDiagPluginConfig& config) {
    if (config.ConfigTypeId() == nullptr || std::strcmp(config.ConfigTypeId(), "mempoint") != 0) {
        return false;
    }
    const auto& cfg = static_cast<const MempointConfig&>(config);
    config_.pollIntervalMs = cfg.pollIntervalMs;
    health_.lastUpdatedNs = Platform::GetTimestampNs();
    return true;
}

bool SpDiagMempointPlugin::Query(const SpDiagQueryFilter& filter, SpDiagResultSet& result) {
    (void)filter;
    result.rows.clear();
    return true;
}

SpDiagHealthInfo SpDiagMempointPlugin::Health() const {
    return health_;
}

}  // namespace SpDiag

#endif
