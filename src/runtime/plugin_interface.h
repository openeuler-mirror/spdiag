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
#include <memory>
#include <string>
#include <vector>

#include "plugin_types.h"

namespace SpDiag {

struct SpDiagPluginMeta {
    std::string name;
    std::string version;
    std::vector<std::string> featureNames;
    std::string minKernelVersion;
    std::vector<std::string> capabilities;
    PluginResourceReq resourceReq;
    uint32_t featureFlags = 0;
};

struct SpDiagPluginConfig {
    virtual ~SpDiagPluginConfig() = default;
    virtual const char* ConfigTypeId() const = 0;
};

struct SpDiagQueryFilter {
    std::string keyword;
    uint32_t maxRows = 256;
};

struct SpDiagResultRow {
    std::string key;
    std::string value;
};

struct SpDiagResultSet {
    std::vector<SpDiagResultRow> rows;
};

struct SpDiagHealthInfo {
    bool healthy = true;
    std::string message;
    uint64_t lastUpdatedNs = 0;
};

class SpDiagPluginInterface {
public:
    virtual ~SpDiagPluginInterface() = default;

    virtual SpDiagPluginMeta Metadata() const = 0;
    virtual bool Init(const SpDiagPluginConfig& config) = 0;
    virtual bool Start() = 0;
    virtual bool Stop() = 0;
    virtual bool Destroy() = 0;
    virtual bool Reconfig(const SpDiagPluginConfig& config) = 0;
    virtual bool Query(const SpDiagQueryFilter& filter, SpDiagResultSet& result) = 0;
    virtual SpDiagHealthInfo Health() const = 0;
};

using PluginPtr = std::unique_ptr<SpDiagPluginInterface>;

}
