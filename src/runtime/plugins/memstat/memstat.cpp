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

#ifdef SPDIAG_ENABLE_OB_MEMORY

#include "memstat.h"

#include <chrono>
#include <cstring>

#include "spdiag/log_macros.h"

namespace SpDiag {

SpDiagMemstatPlugin::SpDiagMemstatPlugin() = default;

SpDiagMemstatPlugin::~SpDiagMemstatPlugin() {
    Stop();
}

SpDiagPluginMeta SpDiagMemstatPlugin::Metadata() const {
    SpDiagPluginMeta meta;
    meta.name = "memstat";
    meta.version = "1.0.0";
    meta.featureNames = {"FEAT_OB_MEMORY"};
    meta.minKernelVersion = "5.4";
    meta.capabilities = {"CAP_SYS_ADMIN", "CAP_BPF"};
    meta.resourceReq.mapMemoryKb = 512;
    meta.resourceReq.maxProbes = 8;
    meta.resourceReq.maxChannels = 1;
    meta.featureFlags = FEAT_OB_MEMORY;
    return meta;
}

bool SpDiagMemstatPlugin::Init(const SpDiagPluginConfig& config) {
    if (config.ConfigTypeId() == nullptr || std::strcmp(config.ConfigTypeId(), "memstat") != 0) {
        return false;
    }
    std::string err;
    if (!ObEnv::CheckAll(&err)) {
        health_.healthy = false;
        health_.message = err;
        return false;
    }
    config_ = static_cast<const MemstatConfig&>(config);
    if (!config_.bpfContext) {
        health_.healthy = false;
        health_.message = "MemstatConfig.bpfContext is null";
        return false;
    }
    health_.healthy = true;
    health_.message = "memstat initialized";
    health_.lastUpdatedNs = Platform::GetTimestampNs();
    stopRequested_.store(false);
    return true;
}

bool SpDiagMemstatPlugin::Start() {
    stopRequested_.store(false);
    if (!config_.bpfContext->AcquireMallocUprobes()) {
        health_.healthy = false;
        health_.message = "ObBpfContext AcquireMallocUprobes failed";
        SPDIAG_LOG_ERROR(LogModule::PLATFORM, "memstat AcquireMallocUprobes failed for pid %d", config_.targetPid);
        return false;
    }
    int aggFd = config_.bpfContext->GetMapFd("agg");
    int stacksFd = config_.bpfContext->GetMapFd("stacks");
    if (!profiler_.Attach(config_.targetPid, aggFd, stacksFd)) {
        config_.bpfContext->ReleaseMallocUprobes();
        health_.healthy = false;
        health_.message = "MemoryProfiler attach failed";
        SPDIAG_LOG_ERROR(LogModule::PLATFORM, "MemoryProfiler attach failed for pid %d", config_.targetPid);
        return false;
    }
    collectThread_ = std::make_unique<std::thread>(&SpDiagMemstatPlugin::CollectLoop, this);
    health_.healthy = true;
    health_.message = "memstat running";
    health_.lastUpdatedNs = Platform::GetTimestampNs();
    return true;
}

void SpDiagMemstatPlugin::CollectLoop() {
    while (!stopRequested_.load()) {
        if (config_.dataSink) {
            profiler_.Collect(config_.dataSink);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.pollIntervalMs));
    }
}

bool SpDiagMemstatPlugin::Stop() {
    stopRequested_.store(true);
    if (collectThread_ && collectThread_->joinable()) {
        collectThread_->join();
    }
    collectThread_.reset();
    profiler_.Detach();
    if (config_.bpfContext) {
        config_.bpfContext->ReleaseMallocUprobes();
    }
    health_.healthy = true;
    health_.message = "memstat stopped";
    health_.lastUpdatedNs = Platform::GetTimestampNs();
    return true;
}

bool SpDiagMemstatPlugin::Destroy() {
    Stop();
    health_.healthy = true;
    health_.message = "memstat destroyed";
    health_.lastUpdatedNs = Platform::GetTimestampNs();
    return true;
}

bool SpDiagMemstatPlugin::Reconfig(const SpDiagPluginConfig& config) {
    if (config.ConfigTypeId() == nullptr || std::strcmp(config.ConfigTypeId(), "memstat") != 0) {
        return false;
    }
    const auto& cfg = static_cast<const MemstatConfig&>(config);
    config_.pollIntervalMs = cfg.pollIntervalMs;
    health_.lastUpdatedNs = Platform::GetTimestampNs();
    return true;
}

bool SpDiagMemstatPlugin::Query(const SpDiagQueryFilter& filter, SpDiagResultSet& result) {
    (void)filter;
    result.rows.clear();
    return true;
}

SpDiagHealthInfo SpDiagMemstatPlugin::Health() const {
    return health_;
}

}  // namespace SpDiag

#endif
