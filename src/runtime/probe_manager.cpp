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

#include "probe_manager.h"

#include <algorithm>

namespace SpDiag {

ProbeHandle ProbeManager::RegisterProbe(const ProbeDesc& probeDesc) {
    std::lock_guard<std::mutex> guard(mutex_);

    const ProbeHandle handle = nextProbeHandle_++;
    ProbeRecord record;
    record.desc = probeDesc;
    probeTable_.emplace(handle, std::move(record));
    return handle;
}

bool ProbeManager::UnregisterProbe(ProbeHandle handle) {
    std::lock_guard<std::mutex> guard(mutex_);
    return probeTable_.erase(handle) > 0;
}

bool ProbeManager::ReserveProbe(const std::string& pluginName, const ProbeDesc& probeDesc, ProbeHandle& handleOut) {
    std::lock_guard<std::mutex> guard(mutex_);

    const std::string key = BuildProbeKey(probeDesc);
    if (probeKeyToHandle_.find(key) != probeKeyToHandle_.end()) {
        return false;
    }

    ProbeDesc desc = probeDesc;
    desc.ownerPlugin = pluginName;
    const ProbeHandle handle = nextProbeHandle_++;
    ProbeRecord record;
    record.desc = desc;
    probeTable_.emplace(handle, std::move(record));

    probeKeyToHandle_[key] = handle;
    handleToProbeKey_[handle] = key;
    pluginProbes_[pluginName].push_back(handle);
    handleOut = handle;
    return true;
}

bool ProbeManager::ReleaseProbe(const std::string& pluginName, ProbeHandle handle) {
    std::lock_guard<std::mutex> guard(mutex_);

    if (handle == 0) {
        return false;
    }

    auto hit = handleToProbeKey_.find(handle);
    if (hit == handleToProbeKey_.end()) {
        return false;
    }

    probeTable_.erase(handle);

    const std::string& key = hit->second;
    probeKeyToHandle_.erase(key);
    handleToProbeKey_.erase(hit);

    auto pit = pluginProbes_.find(pluginName);
    if (pit != pluginProbes_.end()) {
        auto& handles = pit->second;
        auto it = std::find(handles.begin(), handles.end(), handle);
        if (it != handles.end()) {
            handles.erase(it);
        }
    }
    return true;
}

void ProbeManager::CleanupPluginProbes(const std::string& pluginName) {
    std::lock_guard<std::mutex> guard(mutex_);

    auto it = pluginProbes_.find(pluginName);
    if (it == pluginProbes_.end()) {
        return;
    }

    for (ProbeHandle handle : it->second) {
        auto hit = handleToProbeKey_.find(handle);
        if (hit != handleToProbeKey_.end()) {
            probeKeyToHandle_.erase(hit->second);
            handleToProbeKey_.erase(hit);
        }
        probeTable_.erase(handle);
    }

    pluginProbes_.erase(it);
}

bool ProbeManager::HasPlugin(const std::string& pluginName) const {
    auto it = pluginProbes_.find(pluginName);
    return it != pluginProbes_.end() && !it->second.empty();
}

std::string ProbeManager::BuildProbeKey(const ProbeDesc& probeDesc) const {
    return std::to_string(static_cast<int>(probeDesc.type)) + ":" +
           probeDesc.targetBinary + ":" +
           probeDesc.symbolName + ":" +
           std::to_string(probeDesc.targetPid) + ":" +
           std::to_string(probeDesc.offset);
}

}
