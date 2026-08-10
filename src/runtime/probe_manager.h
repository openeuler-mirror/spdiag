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

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "probe_types.h"

namespace SpDiag {

class ProbeManager {
public:
    ProbeManager() = default;

    ProbeHandle RegisterProbe(const ProbeDesc& probeDesc);
    bool UnregisterProbe(ProbeHandle handle);

    bool ReserveProbe(const std::string& pluginName, const ProbeDesc& probeDesc, ProbeHandle& handleOut);
    bool ReleaseProbe(const std::string& pluginName, ProbeHandle handle);
    void CleanupPluginProbes(const std::string& pluginName);

    bool HasPlugin(const std::string& pluginName) const;

private:
    std::string BuildProbeKey(const ProbeDesc& probeDesc) const;

    struct ProbeRecord {
        ProbeDesc desc;
    };

    std::mutex mutex_;
    ProbeHandle nextProbeHandle_ = 1;
    std::unordered_map<ProbeHandle, ProbeRecord> probeTable_;
    std::unordered_map<std::string, ProbeHandle> probeKeyToHandle_;
    std::unordered_map<ProbeHandle, std::string> handleToProbeKey_;
    std::unordered_map<std::string, std::vector<ProbeHandle>> pluginProbes_;
};

}
