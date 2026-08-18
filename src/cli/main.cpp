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

#include "cli_config.h"
#include "display_engine.h"
#include "sort_spec.h"
#include "manager/manager.h"
#include "runtime/plugins/custom_perfpoint/custom_perfpoint.h"
#include "spdiag/log_macros.h"
#include "spdiag/logger.h"
#include "spdiag/signal_watcher.h"
#ifdef SPDIAG_ENABLE_OB_MEMORY
#include "runtime/plugins/memstat/memstat.h"
#endif
#ifdef SPDIAG_ENABLE_OB_CACHE
#include "runtime/plugins/cachestat/cachestat.h"
#endif
#ifdef SPDIAG_ENABLE_MEMPOINT
#include "runtime/plugins/mempoint/mempoint.h"
#endif

#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

using namespace SpDiag;

namespace {

bool CheckShmAliveOrWarn(const SpDiagManager& manager, const std::string& shmName) {
    if (!manager.CheckShmAlive(shmName)) {
        SPDIAG_LOG_ERROR(SpDiag::LogModule::CLI, "spdiag is not running. Please run 'spdiag start' first.");
        return false;
    }
    return true;
}

DisplayOptions BuildDisplayOptions(const Cli::CliConfig& config) {
    DisplayOptions opts;
    opts.detail = config.showDetail;
    opts.coreStart = config.coreStart;
    opts.coreEnd = config.coreEnd;
    opts.levelFilter = config.levelFilter;
    opts.levelOp = config.levelOp;
    opts.hasLevelRange = config.hasLevelRange;
    opts.levelRangeStart = config.levelRangeStart;
    opts.levelRangeEnd = config.levelRangeEnd;
    if (!config.sortSpec.empty()) {
        std::string err;
        opts.sortKeys = Cli::ParseSortSpec(config.sortSpec, "perfpoint", err);
    }
    if (opts.sortKeys.empty()) {
        opts.sortKeys = Cli::DefaultPerfPointSortKeys();
    }
    opts.filterGlobs = config.filterGlobs;
    return opts;
}

void DisplayResults(DisplayEngine& display, const CollectResult& result,
                    const Cli::CliConfig& config, const DisplayOptions& opts) {
    ShmInfo info;
    info.numCores = result.numCores;
    info.maxSlots = result.maxSlots;
    info.maxPrograms = static_cast<uint32_t>(result.programs.size());
    auto* kpn = reinterpret_cast<const char(*)[nameLen]>(result.keyPointNameData.data());
    auto* kmn = reinterpret_cast<const char(*)[nameLen]>(result.keyModuleNameData.data());
    const ProgramEntry* programs = result.programs.empty() ? nullptr : result.programs.data();
    uint32_t numPrograms = static_cast<uint32_t>(result.programs.size());

    if (result.aggregated.empty() && result.delta.empty()) {
        if (config.showDetail) {
            display.ShowEmptyDetail();
        } else if (config.coreStart >= 0) {
            display.ShowRawTable(info, result.shards, programs, numPrograms, kpn, kmn, config.coreStart, config.coreEnd);
        } else {
            display.ShowEmptySummary();
        }
        return;
    }

    if (config.showDetail) {
        display.ShowDetail(info, result.shards, programs, numPrograms, kpn, kmn, opts);
    } else if (config.coreStart >= 0) {
        display.ShowRawTable(info, result.shards, programs, numPrograms, kpn, kmn, config.coreStart, config.coreEnd);
    } else {
        display.ShowSummary(result.aggregated, opts);
    }
}

void RegisterPlugins(SpDiagManager& manager) {
    manager.RegisterPluginFactory("custom_perfpoint", []() -> PluginPtr {
        return std::make_unique<CustomPerfpoint>();
    });
#ifdef SPDIAG_ENABLE_OB_MEMORY
    manager.RegisterPluginFactory("memstat", []() -> PluginPtr {
        return std::make_unique<SpDiagMemstatPlugin>();
    });
#endif
#ifdef SPDIAG_ENABLE_OB_CACHE
    manager.RegisterPluginFactory("cachestat", []() -> PluginPtr {
        return std::make_unique<SpDiagCachestatPlugin>();
    });
#endif
#ifdef SPDIAG_ENABLE_MEMPOINT
    manager.RegisterPluginFactory("mempoint", []() -> PluginPtr {
        return std::make_unique<SpDiagMempointPlugin>();
    });
#endif
}

}

int main(int argc, char* argv[]) {
    auto config = Cli::CliConfig::ParseArgs(argc, argv);

    // 设置运行时日志级别（如有指定 --log-level 参数）
    if (!config.logLevelStr.empty()) {
        auto level = SpDiag::ParseLogLevelName(config.logLevelStr);
        if (level.has_value()) {
            SpDiag::LoggerCore::GetInstance().SetRuntimeLogLevel(*level);
        } else {
            std::cerr << "Invalid --log-level value: '" << config.logLevelStr
                      << "'. Valid options: DEBUG/INFO/WARN/ERROR. "
                      << "Using default (ERROR).\n";
        }
    }

    SpDiagManager manager;
    RegisterPlugins(manager);
    DisplayEngine display;

    // 信号观察器：长运行命令（WATCH/MEMSTAT/CACHESTAT/MEMPOINT）中启动，
    // 收到 SIGINT/SIGTERM 时自动刷盘日志，避免缓冲日志丢失
    SignalWatcher signal_watcher;

    switch (config.command) {
        case Cli::Command::SHOW: {
            if (!CheckShmAliveOrWarn(manager, config.shmName)) return 1;
            if (config.HasAnyFlag(FlagBit(Cli::UserFlag::CSV_OUTPUT))) {
                display.GetCsvWriter().SetOutputDir(config.csvOutputDir);
            }
            auto result = manager.CollectAndAggregate(config);
#ifdef SPDIAG_ENABLE_PERFLOG
            if (config.perflogShow) {
                ShmInfo info;
                info.numCores = result.numCores;
                info.maxSlots = result.maxSlots;
                info.maxPrograms = static_cast<uint32_t>(result.programs.size());
                info.perflogEnabled = result.perflogEnabled;
                info.perflogBootCounter = result.perflogBootCounter;
                info.perflogBootSystemNs = result.perflogBootSystemNs;
                info.perflogTimerFreq = result.perflogTimerFreq;
                auto* kpn = reinterpret_cast<const char(*)[nameLen]>(result.keyPointNameData.data());
                auto* kmn = reinterpret_cast<const char(*)[nameLen]>(result.keyModuleNameData.data());
                display.ShowPerfLog(info, result.shards, result.programs.data(),
                                    static_cast<uint32_t>(result.programs.size()), kpn, kmn);
                return 0;
            }
#endif
            auto opts = BuildDisplayOptions(config);
            DisplayResults(display, result, config, opts);
            return 0;
        }
        case Cli::Command::WATCH: {
            if (!CheckShmAliveOrWarn(manager, config.shmName)) return 1;
            if (config.HasAnyFlag(FlagBit(Cli::UserFlag::CSV_OUTPUT))) {
                display.GetCsvWriter().SetOutputDir(config.csvOutputDir);
                display.GetCsvWriter().SetPersistent(true);  // watch 模式同一会话只生成一个 CSV
            }
            signal_watcher.BlockAndStart({SIGINT, SIGTERM}, []() {
                LoggerCore::GetInstance().FlushAll();
            });
            display.ClearScreen();
            display.MoveCursorHome();
            while (!signal_watcher.IsInterrupted()) {
                display.ClearScreen();
                display.MoveCursorHome();

                auto result = manager.CollectAndAggregate(config);
#ifdef SPDIAG_ENABLE_PERFLOG
                if (config.perflogShow) {
                    ShmInfo info;
                    info.numCores = result.numCores;
                    info.maxSlots = result.maxSlots;
                    info.maxPrograms = static_cast<uint32_t>(result.programs.size());
                    info.perflogEnabled = result.perflogEnabled;
                    info.perflogBootCounter = result.perflogBootCounter;
                    info.perflogBootSystemNs = result.perflogBootSystemNs;
                    info.perflogTimerFreq = result.perflogTimerFreq;
                    auto* kpn = reinterpret_cast<const char(*)[nameLen]>(result.keyPointNameData.data());
                    auto* kmn = reinterpret_cast<const char(*)[nameLen]>(result.keyModuleNameData.data());
                    display.ShowPerfLog(info, result.shards, result.programs.data(),
                                        static_cast<uint32_t>(result.programs.size()), kpn, kmn);
                }
                else
#endif
                {
                    auto opts = BuildDisplayOptions(config);
                    DisplayResults(display, result, config, opts);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(config.watchIntervalMs));
            }
            display.GetCsvWriter().ForceEndSession();
            return 0;
        }
        case Cli::Command::START:
            return manager.StartShm(config) ? 0 : 1;
        case Cli::Command::STOP:
            return manager.StopShm(config) ? 0 : 1;
        case Cli::Command::RESTART:
            return manager.RestartShm(config) ? 0 : 1;
        case Cli::Command::STATUS:
            return manager.ShmStatus(config) ? 0 : 1;
        case Cli::Command::CLEAR:
            if (!CheckShmAliveOrWarn(manager, config.shmName)) return 1;
            return manager.ClearData(config) ? 0 : 1;
        case Cli::Command::HISTORY: {
            if (config.HasAnyFlag(FlagBit(Cli::UserFlag::CSV_OUTPUT))) {
                display.GetCsvWriter().SetOutputDir(config.csvOutputDir);
            }
            auto records = manager.QueryHistory(config);
            if (records.empty()) {
                std::cout << "No dump history available" << std::endl;
                return 0;
            }

            if (config.historyIndex >= 0) {
                if (static_cast<size_t>(config.historyIndex) >= records.size()) {
                    std::cerr << "Invalid history index: " << config.historyIndex
                              << " (available: 0-" << (records.size() - 1) << ")" << std::endl;
                    return 1;
                }
                display.ShowHistoryDetail(records[config.historyIndex]);
            } else {
                display.ShowHistory(records, config.historyCount);
            }
            return 0;
        }
        case Cli::Command::MEMSTAT:
        case Cli::Command::CACHESTAT:
        case Cli::Command::MEMPOINT: {
#ifdef SPDIAG_HAS_OB
            if (config.HasAnyFlag(FlagBit(Cli::UserFlag::CSV_OUTPUT))) {
                display.GetCsvWriter().SetOutputDir(config.csvOutputDir);
                display.GetCsvWriter().SetPersistent(true);  // watch 模式同一会话只生成一个 CSV
            }
            signal_watcher.BlockAndStart({SIGINT, SIGTERM}, []() {
                LoggerCore::GetInstance().FlushAll();
            });
            bool memMode = (config.command == Cli::Command::MEMSTAT);
            bool mpMode = (config.command == Cli::Command::MEMPOINT);
            if (!manager.StartObCollect(config)) return 1;
            while (!signal_watcher.IsInterrupted()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(config.obIntervalMs));
                auto outputs = manager.CollectObData();
                if (!outputs.empty()) {
                    if (mpMode) {
                        std::vector<Cli::SortKey> mpSortKeys;
                        if (!config.sortSpec.empty()) {
                            std::string sortErr;
                            mpSortKeys = Cli::ParseSortSpec(config.sortSpec, "memstat", sortErr);
                        }
                        if (mpSortKeys.empty()) {
                            mpSortKeys = Cli::DefaultMemPointSortKeys();
                        }
                        display.ShowMemPoint(outputs, config.obHumanReadable,
                                             config.obTop, mpSortKeys, config.filterGlobs,
                                             config.obShowFields);
                    } else if (memMode) {
                        std::vector<Cli::SortKey> memSortKeys;
                        if (!config.sortSpec.empty()) {
                            std::string sortErr;
                            memSortKeys = Cli::ParseSortSpec(config.sortSpec, "memstat", sortErr);
                        }
                        if (memSortKeys.empty()) {
                            memSortKeys = Cli::DefaultMemstatSortKeys();
                        }
                        display.ShowObMemory(outputs, config.obHumanReadable, config.obNoFilter,
                                             config.obDetail, config.obGroupBy, config.obTop,
                                             config.obShowFields, memSortKeys, config.filterGlobs);
                    } else {
                        display.ShowObCache(outputs, config.obCumulative);
                    }
                }
            }
            display.GetCsvWriter().ForceEndSession();
            manager.StopObCollect();
            return 0;
#else
            std::cerr << "OB observation commands require OB features to be enabled at compile time.\n";
            return 1;
#endif
        }
        default:
            return 1;
    }
}
