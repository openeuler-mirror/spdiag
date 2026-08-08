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

#include "custom_perfpoint.h"

#include "plugin_types.h"
#include "spdiag/shm_layout.h"
#include "spdiag/log_macros.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

namespace SpDiag {

SpDiagPluginMeta CustomPerfpoint::Metadata() const {
    SpDiagPluginMeta meta;
    meta.name = "custom_perfpoint";
    meta.version = "1.0.0";
    meta.featureNames = {"FEAT_CUSTOM_UPROBE", "FEAT_CUSTOM_TRACEPOINT"};
    meta.minKernelVersion = "4.19";
    meta.capabilities = {"CAP_SYS_ADMIN"};
    meta.resourceReq.mapMemoryKb = 512;
    meta.resourceReq.maxProbes = 64;
    meta.resourceReq.maxChannels = 1;
    meta.featureFlags = FEAT_CUSTOM_UPROBE | FEAT_CUSTOM_TRACEPOINT;
    return meta;
}

bool CustomPerfpoint::Init(const SpDiagPluginConfig& config) {
    if (config.ConfigTypeId() == nullptr || std::strcmp(config.ConfigTypeId(), "custom_perfpoint") != 0) {
        return false;
    }
    const auto& perfConfig = static_cast<const CustomPerfpointConfig&>(config);

    config_ = perfConfig;
    health_.healthy = true;
    health_.message = "custom_perfpoint initialized";
    health_.lastUpdatedNs = Platform::GetTimestampNs();
    stopRequested_.store(false);
    return true;
}

bool CustomPerfpoint::Start() {
    stopRequested_.store(false);

    bool ok = false;
    if (config_.watchMode) {
        ok = RunWatchCollect();
    } else {
        ok = RunCollect();
    }

    health_.healthy = ok;
    health_.message = ok ? "custom_perfpoint running" : "custom_perfpoint failed";
    health_.lastUpdatedNs = Platform::GetTimestampNs();
    return ok;
}

bool CustomPerfpoint::Stop() {
    stopRequested_.store(true);
    health_.healthy = true;
    health_.message = "custom_perfpoint stopped";
    health_.lastUpdatedNs = Platform::GetTimestampNs();
    return true;
}

bool CustomPerfpoint::Destroy() {
    stopRequested_.store(true);
    health_.healthy = true;
    health_.message = "custom_perfpoint destroyed";
    health_.lastUpdatedNs = Platform::GetTimestampNs();
    return true;
}

bool CustomPerfpoint::Reconfig(const SpDiagPluginConfig& config) {
    if (config.ConfigTypeId() == nullptr || std::strcmp(config.ConfigTypeId(), "custom_perfpoint") != 0) {
        return false;
    }
    config_ = static_cast<const CustomPerfpointConfig&>(config);
    health_.lastUpdatedNs = Platform::GetTimestampNs();
    return true;
}

bool CustomPerfpoint::Query(const SpDiagQueryFilter& filter, SpDiagResultSet& result) {
    (void)filter;
    result.rows.clear();
    SpDiagResultRow row;
    row.key = "plugin";
    row.value = "custom_perfpoint";
    result.rows.push_back(std::move(row));
    return true;
}

SpDiagHealthInfo CustomPerfpoint::Health() const {
    return health_;
}

bool CustomPerfpoint::RunCollect() {
    ShmReader reader;
    if (!reader.Open(config_.shmName)) {
        SPDIAG_LOG_ERROR(LogModule::RUNTIME, "Failed to open shared memory: %s", config_.shmName.c_str());
        return false;
    }

    const ShmHeader& hdr = reader.Header();

    ShardBuffer shards;
    bool snapshotConsistent = reader.SnapshotShards(shards);
    if (!snapshotConsistent && config_.strictSnapshot) {
        SPDIAG_LOG_ERROR(LogModule::RUNTIME, "Failed to snapshot shards (strict mode)");
        reader.Close();
        return false;
    }

    if (config_.dataSink != nullptr) {
        config_.dataSink->Submit("numCores", std::to_string(hdr.numCores));
        config_.dataSink->Submit("snapshotConsistent", snapshotConsistent ? "true" : "false");

        uint64_t totalStats = 0;
        for (uint32_t i = 0; i < hdr.numCores; ++i) {
            totalStats += hdr.maxSlots;
        }
        config_.dataSink->Submit("statCount", std::to_string(totalStats));
    }

    reader.Close();
    return true;
}

bool CustomPerfpoint::RunWatchCollect() {
    while (!stopRequested_.load()) {
        if (!RunCollect()) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.watchIntervalMs));
    }
    return true;
}

}
