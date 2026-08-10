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

#include "data_pipeline.h"
#include "data_sink.h"
#include "probe_manager.h"
#include "probe_types.h"
#include "symbol_resolver.h"

namespace SpDiag {

class SpDiagProcessor {
public:
    SpDiagProcessor() = default;

    ProbeHandle RegisterProbe(const ProbeDesc& probeDesc);
    bool UnregisterProbe(ProbeHandle handle);

    uint32_t CreateChannel(const std::string& pluginName);
    bool DestroyChannel(uint32_t channelId);
    bool SubmitData(uint32_t channelId, const ProcessorData& data);

    std::unique_ptr<DataSink> CreateSink(uint32_t channelId);

    std::vector<ProcessorOutput> GetOutput();
    SymbolResolver& GetSymbolResolver();

    bool ClearSharedMemory(const std::string& shmName);

    ProbeManager& GetProbeManager();
    DataPipeline& GetDataPipeline();

private:
    ProbeManager probeManager_;
    DataPipeline dataPipeline_;
};

}
