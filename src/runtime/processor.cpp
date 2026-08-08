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

#include "processor.h"

#include "sdk/platform.h"
#include "sdk/shm_probe.h"
#include "sdk/shm_writer.h"
#include "spdiag/shm_layout.h"

namespace SpDiag {

ProbeHandle SpDiagProcessor::RegisterProbe(const ProbeDesc& probeDesc) {
    return probeManager_.RegisterProbe(probeDesc);
}

bool SpDiagProcessor::UnregisterProbe(ProbeHandle handle) {
    return probeManager_.UnregisterProbe(handle);
}

uint32_t SpDiagProcessor::CreateChannel(const std::string& pluginName) {
    return dataPipeline_.CreateChannel(pluginName);
}

bool SpDiagProcessor::DestroyChannel(uint32_t channelId) {
    return dataPipeline_.DestroyChannel(channelId);
}

bool SpDiagProcessor::SubmitData(uint32_t channelId, const ProcessorData& data) {
    return dataPipeline_.SubmitData(channelId, data);
}

std::unique_ptr<DataSink> SpDiagProcessor::CreateSink(uint32_t channelId) {
    return dataPipeline_.CreateSink(channelId);
}

std::vector<ProcessorOutput> SpDiagProcessor::GetOutput() {
    return dataPipeline_.GetOutput();
}

SymbolResolver& SpDiagProcessor::GetSymbolResolver() {
    return dataPipeline_.GetSymbolResolver();
}

ProbeManager& SpDiagProcessor::GetProbeManager() {
    return probeManager_;
}

DataPipeline& SpDiagProcessor::GetDataPipeline() {
    return dataPipeline_;
}

bool SpDiagProcessor::ClearSharedMemory(const std::string& shmName) {
    auto probe = ProbeShmHeader(shmName);
    if (!probe.valid) {
        return false;
    }

    ShmWriter writer;
    if (!writer.Open(shmName, probe.totalSize)) {
        return false;
    }

    ShmHeader* hdr = writer.Header();
    hdr->dumpSeqNo.fetch_add(1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);

    for (uint32_t i = 0; i < hdr->numCores; ++i) {
        void* shardPtr = writer.ShardPtr(0, i);
        PerfStat* stats = ShmShardStats(shardPtr);
        for (uint32_t j = 0; j < hdr->maxSlots; ++j) {
            stats[j].reset();
        }
    }
    std::atomic_thread_fence(std::memory_order_release);
    hdr->dumpSeqNo.fetch_add(1, std::memory_order_relaxed);
    writer.WriteHeader(*hdr);
    return true;
}

}
