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

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "data_sink.h"
#include "plugin_interface.h"
#include "probe_types.h"
#include "processor.h"

namespace SpDiag {

struct PluginRecord {
    PluginPtr plugin;
    SpDiagPluginMeta meta;
    PluginState state = PluginState::REGISTERED;
    std::shared_ptr<SpDiagPluginConfig> config;
    PluginResourceReq resourceUsage;
    uint64_t instanceId = 0;
    uint32_t channelId = 0;
    std::unique_ptr<DataSink> dataSink;
};

struct PluginEventListener {
    virtual ~PluginEventListener() = default;
    virtual void OnPluginStateChanged(const std::string& name, PluginState from, PluginState to) = 0;
    virtual void OnPluginError(const std::string& name, const std::string& error) = 0;
};

using PluginFactory = std::function<PluginPtr()>;

class SpDiagPluginManager {
public:
    explicit SpDiagPluginManager(SpDiagProcessor& processor);

    bool RegisterPlugin(PluginPtr plugin);
    bool UnregisterPlugin(const std::string& name);

    bool InitPlugin(const std::string& name, std::shared_ptr<SpDiagPluginConfig> config);
    bool StartPlugin(const std::string& name);
    bool StopPlugin(const std::string& name);
    bool DestroyPlugin(const std::string& name);

    bool StartAll();
    bool StopAll();
    bool DestroyAll();

    bool QueryPlugin(const std::string& name, const SpDiagQueryFilter& filter, SpDiagResultSet& result);
    bool GetPluginHealth(const std::string& name, SpDiagHealthInfo& healthInfo) const;

    bool ReserveProbe(const std::string& pluginName, const ProbeDesc& probeDesc, ProbeHandle& handleOut);
    bool ReleaseProbe(const std::string& pluginName, ProbeHandle handle);

    DataSink* GetPluginSink(const std::string& name) const;

    void RegisterFactory(const std::string& pluginType, PluginFactory factory);
    bool EnsurePlugin(const std::string& pluginType, std::shared_ptr<SpDiagPluginConfig> config);

    void AddEventListener(PluginEventListener* listener);
    void RemoveEventListener(PluginEventListener* listener);

    template <typename ConfigT>
    bool InitPluginTyped(const std::string& name, const ConfigT& config) {
        auto cfg = std::make_shared<ConfigT>(config);
        return InitPlugin(name, std::move(cfg));
    }

private:
    bool IsTransitionValid(PluginState from, PluginState to) const;
    bool CheckResourceBudget(const PluginResourceReq& req) const;
    void NotifyStateChanged(const std::string& name, PluginState from, PluginState to);
    void NotifyError(const std::string& name, const std::string& error);

    SpDiagProcessor& processor_;
    uint64_t nextInstanceId_ = 1;
    PluginResourceReq budget_{4096, 256, 64};
    std::unordered_map<std::string, PluginRecord> plugins_;
    std::unordered_map<std::string, PluginFactory> factories_;
    std::vector<PluginEventListener*> listeners_;
};

}
