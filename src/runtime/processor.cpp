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
#include "sdk/registry_file_guard.h"
#include "sdk/shm_probe.h"
#include "sdk/shm_writer.h"
#include "spdiag/shm_layout.h"
#include <chrono>
#include <thread>
#include <vector>

namespace SpDiag {

namespace {
bool AcquireDumpWindow(ShmHeader* header) {
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(DumpWindowTimeoutMs(
            header->perCoreTopNCapacity, header->registeredPrograms));
    while (true) {
        uint64_t expected = header->dumpSeqNo.load(std::memory_order_acquire);
        if ((expected & 1U) == 0 && header->dumpSeqNo.compare_exchange_weak(
                expected, expected + 1, std::memory_order_acq_rel)) return true;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
}

}

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
    int lockError = 0;
    auto registryGuard = RegistryFileGuard::AcquireFor(
        writer.Fd(), std::chrono::seconds(10), &lockError);
    if (!registryGuard) return false;
    auto* registry = reinterpret_cast<ProgramEntry*>(
        reinterpret_cast<uint8_t*>(hdr) + hdr->registryOffset);
    std::vector<ProgramTarget> targets;
    for (uint32_t i = 0; i < hdr->registeredPrograms; ++i) {
        const int32_t pid = __atomic_load_n(&registry[i].pid,
                                             __ATOMIC_ACQUIRE);
        if (registry[i].active && pid > 0 &&
            Platform::ProcessInstanceIsAlive(
                pid, registry[i].processStartTimeTicks)) {
            targets.push_back(
                {i, pid, registry[i].processStartTimeTicks});
        }
    }
    if (!AcquireDumpWindow(hdr)) return false;
    const uint64_t request = __atomic_add_fetch(
        &hdr->generation, 1U, __ATOMIC_ACQ_REL);

    // The generation change is the clear linearization point.  Reset the
    // published data while the writer window is held, then release the window
    // before waiting: producers need that same window to finish a dump that
    // began before this request and observe the new generation.
    for (uint32_t i = 0; i < hdr->numCores; ++i) {
        void* shardPtr = writer.ShardPtr(0, i);
        PerfStat* stats = ShmShardStats(shardPtr);
        for (uint32_t j = 0; j < hdr->maxSlots; ++j) stats[j].reset();
    }
    if (GlobalPerfStat* global = writer.GlobalAccumPtr()) {
        for (uint32_t i = 0; i < hdr->globalAccumCount; ++i)
            global[i].stat.reset();
    }
    std::atomic_thread_fence(std::memory_order_release);
    hdr->dumpSeqNo.fetch_add(1, std::memory_order_release);
    registryGuard.reset();

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(10);
    while (true) {
        bool complete = true;
        for (const ProgramTarget& target : targets) {
            const ProgramEntry& entry = registry[target.index];
            if (__atomic_load_n(&entry.pid, __ATOMIC_ACQUIRE) != target.pid ||
                entry.processStartTimeTicks != target.processStartTimeTicks ||
                !Platform::ProcessInstanceIsAlive(
                    target.pid, target.processStartTimeTicks)) continue;
            if (__atomic_load_n(&entry.clearAckGeneration,
                                __ATOMIC_ACQUIRE) < request) {
                complete = false;
                break;
            }
        }
        if (complete) break;
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

}
