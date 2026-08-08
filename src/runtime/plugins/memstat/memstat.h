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

// Memstat plugin: wraps MemoryProfiler (libc malloc/free uprobes via the shared
// ob_point BPF object) as an independent SpDiagPluginInterface. The BPF object
// is owned by ObBpfContext (injected), so this plugin no longer loads BPF
// itself and no longer couples to mempoint.

#ifdef SPDIAG_ENABLE_OB_MEMORY

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "data_sink.h"
#include "memory_profiler.h"
#include "ob_bpf_context.h"
#include "ob_env.h"
#include "plugin_interface.h"
#include "sdk/platform.h"

namespace SpDiag {

struct MemstatConfig : SpDiagPluginConfig {
    const char* ConfigTypeId() const override { return "memstat"; }

    int32_t targetPid = -1;
    std::string libcPath = "/usr/lib64/libc.so.6";
    uint32_t pollIntervalMs = 1000;
    DataSink* dataSink = nullptr;
    ObBpfContext* bpfContext = nullptr;   // injected, owned by SpDiagManager
};

class SpDiagMemstatPlugin final : public SpDiagPluginInterface {
public:
    SpDiagMemstatPlugin();
    ~SpDiagMemstatPlugin() override;

    SpDiagPluginMeta Metadata() const override;
    bool Init(const SpDiagPluginConfig& config) override;
    bool Start() override;
    bool Stop() override;
    bool Destroy() override;
    bool Reconfig(const SpDiagPluginConfig& config) override;
    bool Query(const SpDiagQueryFilter& filter, SpDiagResultSet& result) override;
    SpDiagHealthInfo Health() const override;

private:
    void CollectLoop();

    MemstatConfig config_;
    SpDiagHealthInfo health_;
    MemoryProfiler profiler_;
    std::atomic<bool> stopRequested_{false};
    std::unique_ptr<std::thread> collectThread_;
};

}  // namespace SpDiag

#endif
