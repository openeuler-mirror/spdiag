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

#include "plugin_manager.h"

#include <algorithm>

namespace SpDiag {

SpDiagPluginManager::SpDiagPluginManager(SpDiagProcessor& processor)
    : processor_(processor) {}

bool SpDiagPluginManager::RegisterPlugin(PluginPtr plugin) {
    if (!plugin) {
        return false;
    }

    SpDiagPluginMeta meta = plugin->Metadata();
    if (meta.name.empty()) {
        return false;
    }
    if (plugins_.find(meta.name) != plugins_.end()) {
        return false;
    }
    if (!CheckResourceBudget(meta.resourceReq)) {
        return false;
    }

    uint32_t channelId = processor_.CreateChannel(meta.name);
    std::unique_ptr<DataSink> sink = processor_.CreateSink(channelId);

    PluginRecord record;
    record.meta = meta;
    record.resourceUsage = meta.resourceReq;
    record.instanceId = nextInstanceId_++;
    record.plugin = std::move(plugin);
    record.state = PluginState::REGISTERED;
    record.channelId = channelId;
    record.dataSink = std::move(sink);

    plugins_.emplace(meta.name, std::move(record));
    return true;
}

bool SpDiagPluginManager::UnregisterPlugin(const std::string& name) {
    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        return false;
    }

    PluginState state = it->second.state;
    if (state != PluginState::REGISTERED && state != PluginState::DESTROYED) {
        return false;
    }

    processor_.GetProbeManager().CleanupPluginProbes(name);
    uint32_t channelId = it->second.channelId;
    plugins_.erase(it);
    if (channelId != 0) {
        processor_.DestroyChannel(channelId);
    }
    return true;
}

bool SpDiagPluginManager::InitPlugin(const std::string& name, std::shared_ptr<SpDiagPluginConfig> config) {
    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        return false;
    }
    if (!IsTransitionValid(it->second.state, PluginState::INITIALIZED)) {
        return false;
    }

    if (!it->second.plugin->Init(*config)) {
        NotifyError(name, "Init failed");
        return false;
    }

    PluginState from = it->second.state;
    it->second.config = std::move(config);
    it->second.state = PluginState::INITIALIZED;
    NotifyStateChanged(name, from, PluginState::INITIALIZED);
    return true;
}

bool SpDiagPluginManager::StartPlugin(const std::string& name) {
    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        return false;
    }
    if (!IsTransitionValid(it->second.state, PluginState::RUNNING)) {
        return false;
    }

    if (!it->second.plugin->Start()) {
        NotifyError(name, "Start failed");
        return false;
    }

    PluginState from = it->second.state;
    it->second.state = PluginState::RUNNING;
    NotifyStateChanged(name, from, PluginState::RUNNING);
    return true;
}

bool SpDiagPluginManager::StopPlugin(const std::string& name) {
    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        return false;
    }
    if (!IsTransitionValid(it->second.state, PluginState::STOPPED)) {
        return false;
    }

    if (!it->second.plugin->Stop()) {
        NotifyError(name, "Stop failed");
        return false;
    }

    PluginState from = it->second.state;
    it->second.state = PluginState::STOPPED;
    NotifyStateChanged(name, from, PluginState::STOPPED);
    return true;
}

bool SpDiagPluginManager::DestroyPlugin(const std::string& name) {
    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        return false;
    }
    if (!IsTransitionValid(it->second.state, PluginState::DESTROYED)) {
        return false;
    }

    if (!it->second.plugin->Destroy()) {
        NotifyError(name, "Destroy failed");
        return false;
    }

    PluginState from = it->second.state;
    it->second.state = PluginState::DESTROYED;
    NotifyStateChanged(name, from, PluginState::DESTROYED);
    return true;
}

bool SpDiagPluginManager::StartAll() {
    bool allOk = true;
    for (auto& [name, record] : plugins_) {
        if (record.state == PluginState::INITIALIZED || record.state == PluginState::STOPPED) {
            if (!StartPlugin(name)) {
                allOk = false;
            }
        }
    }
    return allOk;
}

bool SpDiagPluginManager::StopAll() {
    bool allOk = true;
    for (auto& [name, record] : plugins_) {
        if (record.state == PluginState::RUNNING) {
            if (!StopPlugin(name)) {
                allOk = false;
            }
        }
    }
    return allOk;
}

bool SpDiagPluginManager::DestroyAll() {
    bool allOk = true;
    for (auto& [name, record] : plugins_) {
        if (record.state == PluginState::STOPPED || record.state == PluginState::INITIALIZED) {
            if (!DestroyPlugin(name)) {
                allOk = false;
            }
        }
    }
    return allOk;
}

bool SpDiagPluginManager::QueryPlugin(const std::string& name, const SpDiagQueryFilter& filter, SpDiagResultSet& result) {
    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        return false;
    }

    return it->second.plugin->Query(filter, result);
}

bool SpDiagPluginManager::GetPluginHealth(const std::string& name, SpDiagHealthInfo& healthInfo) const {
    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        return false;
    }

    healthInfo = it->second.plugin->Health();
    return true;
}

bool SpDiagPluginManager::ReserveProbe(const std::string& pluginName, const ProbeDesc& probeDesc, ProbeHandle& handleOut) {
    if (plugins_.find(pluginName) == plugins_.end()) {
        return false;
    }
    return processor_.GetProbeManager().ReserveProbe(pluginName, probeDesc, handleOut);
}

bool SpDiagPluginManager::ReleaseProbe(const std::string& pluginName, ProbeHandle handle) {
    (void)pluginName;
    return processor_.GetProbeManager().ReleaseProbe(pluginName, handle);
}

DataSink* SpDiagPluginManager::GetPluginSink(const std::string& name) const {
    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        return nullptr;
    }
    return it->second.dataSink.get();
}

void SpDiagPluginManager::RegisterFactory(const std::string& pluginType, PluginFactory factory) {
    factories_[pluginType] = std::move(factory);
}

bool SpDiagPluginManager::EnsurePlugin(const std::string& pluginType, std::shared_ptr<SpDiagPluginConfig> config) {
    auto it = plugins_.find(pluginType);
    if (it != plugins_.end()) {
        if (it->second.state == PluginState::RUNNING) {
            return true;
        }
        if (it->second.state == PluginState::INITIALIZED || it->second.state == PluginState::STOPPED) {
            return StartPlugin(pluginType);
        }
        if (it->second.state == PluginState::REGISTERED) {
            if (!InitPlugin(pluginType, config)) {
                return false;
            }
            return StartPlugin(pluginType);
        }
        return false;
    }

    auto fit = factories_.find(pluginType);
    if (fit == factories_.end()) {
        return false;
    }

    auto plugin = fit->second();
    if (!plugin) {
        return false;
    }

    if (!RegisterPlugin(std::move(plugin))) {
        return false;
    }

    if (!InitPlugin(pluginType, std::move(config))) {
        return false;
    }
    return StartPlugin(pluginType);
}

void SpDiagPluginManager::AddEventListener(PluginEventListener* listener) {
    auto it = std::find(listeners_.begin(), listeners_.end(), listener);
    if (it == listeners_.end()) {
        listeners_.push_back(listener);
    }
}

void SpDiagPluginManager::RemoveEventListener(PluginEventListener* listener) {
    auto it = std::find(listeners_.begin(), listeners_.end(), listener);
    if (it != listeners_.end()) {
        listeners_.erase(it);
    }
}

bool SpDiagPluginManager::IsTransitionValid(PluginState from, PluginState to) const {
    if (from == PluginState::REGISTERED && to == PluginState::INITIALIZED) {
        return true;
    }
    if (from == PluginState::INITIALIZED && to == PluginState::RUNNING) {
        return true;
    }
    if (from == PluginState::RUNNING && to == PluginState::STOPPED) {
        return true;
    }
    if (from == PluginState::STOPPED && to == PluginState::RUNNING) {
        return true;
    }
    if ((from == PluginState::STOPPED || from == PluginState::INITIALIZED) && to == PluginState::DESTROYED) {
        return true;
    }
    return false;
}

bool SpDiagPluginManager::CheckResourceBudget(const PluginResourceReq& req) const {
    if (req.mapMemoryKb > budget_.mapMemoryKb) {
        return false;
    }
    if (req.maxProbes > budget_.maxProbes) {
        return false;
    }
    if (req.maxChannels > budget_.maxChannels) {
        return false;
    }
    return true;
}

void SpDiagPluginManager::NotifyStateChanged(const std::string& name, PluginState from, PluginState to) {
    for (auto* listener : listeners_) {
        listener->OnPluginStateChanged(name, from, to);
    }
}

void SpDiagPluginManager::NotifyError(const std::string& name, const std::string& error) {
    for (auto* listener : listeners_) {
        listener->OnPluginError(name, error);
    }
}

}
