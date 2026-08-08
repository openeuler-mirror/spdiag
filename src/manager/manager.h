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

#include <csignal>
#include <memory>
#include <string>
#include <vector>
#include <functional>

#include "cli/cli_config.h"
#include "runtime/plugin_manager.h"
#include "runtime/data_pipeline.h"
#include "aggregator.h"
#include "persistent_aggr.h"
#include "dump_recorder.h"

namespace SpDiag {

struct CollectResult {
    std::vector<AggregatedStat> aggregated;
    std::vector<AggregatedStat> delta;
    std::vector<ProgramEntry> programs;
    std::vector<char> keyPointNameData;
    std::vector<char> keyModuleNameData;
    ShardBuffer shards;
    uint32_t numCores = 0;
    uint32_t maxSlots = 0;
    // 全局 PerfPoint 累加区快照（原始 PerfStat[] 字节，无 coreId 前缀）。
    std::vector<uint8_t> globalAccumData;
    uint32_t globalAccumCount = 0;
#ifdef SPDIAG_ENABLE_PERFLOG
    uint8_t  perflogEnabled = 0;
    uint64_t perflogBootCounter = 0;
    uint64_t perflogBootSystemNs = 0;
    uint64_t perflogTimerFreq = 0;
#endif
};

class ObBpfContext;  // defined under SPDIAG_USES_BPF; shared BPF object for memstat/mempoint

class SpDiagManager {
public:
    SpDiagManager();

    void RegisterPluginFactory(const std::string& name, PluginFactory factory);

    CollectResult CollectAndAggregate(const Cli::CliConfig& config);
    std::vector<DumpRecord> QueryHistory(const Cli::CliConfig& config);
    bool ClearData(const Cli::CliConfig& config);
    bool CheckShmAlive(const std::string& shmName) const;
    bool StartShm(const Cli::CliConfig& config);
    bool StopShm(const Cli::CliConfig& config);
    bool ShmStatus(const Cli::CliConfig& config);
    bool RestartShm(const Cli::CliConfig& config);

    bool StartObCollect(const Cli::CliConfig& config);
    std::vector<ProcessorOutput> CollectObData();
    void StopObCollect();

private:
    bool CreateShm(const Cli::CliConfig& config);
    bool DestroyShm(const std::string& shmName);
    bool EnsureCustomPlugin(const Cli::CliConfig& config);
#if defined(SPDIAG_HAS_OB)
    bool EnsureMemstatPlugin(const Cli::CliConfig& config);
    bool EnsureCachestatPlugin(const Cli::CliConfig& config);
    bool EnsureMempointPlugin(const Cli::CliConfig& config);
#endif

    SpDiagProcessor processor_;
#if defined(SPDIAG_USES_BPF)
    // Declared between processor_ and pluginManager_ so that on destruction
    // pluginManager_ (which drops the plugins and their BPF refs) is destroyed
    // before obBpfContext_ unloads the shared bpf_object.
    std::shared_ptr<ObBpfContext> obBpfContext_;
#endif
    SpDiagPluginManager pluginManager_;
};

}
