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

#include <atomic>
#include <cstdint>
#include <string>

#include "data_sink.h"
#include "plugin_interface.h"
#include "shm_reader.h"
#include "sdk/platform.h"

namespace SpDiag {

struct CustomPerfpointConfig : SpDiagPluginConfig {
    const char* ConfigTypeId() const override { return "custom_perfpoint"; }

    std::string shmName = "/spdiag_shm_default";
    DataSink* dataSink = nullptr;
    bool watchMode = false;
    uint32_t watchIntervalMs = 2000;
    bool strictSnapshot = false;
};

class CustomPerfpoint final : public SpDiagPluginInterface {
public:
    SpDiagPluginMeta Metadata() const override;
    bool Init(const SpDiagPluginConfig& config) override;
    bool Start() override;
    bool Stop() override;
    bool Destroy() override;
    bool Reconfig(const SpDiagPluginConfig& config) override;
    bool Query(const SpDiagQueryFilter& filter, SpDiagResultSet& result) override;
    SpDiagHealthInfo Health() const override;

private:
    bool RunCollect();
    bool RunWatchCollect();

    CustomPerfpointConfig config_;
    SpDiagHealthInfo health_;
    std::atomic<bool> stopRequested_{false};
};

}
