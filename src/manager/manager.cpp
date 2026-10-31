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

#include "manager.h"

#include "file_lock_guard.h"
#include "sdk/platform.h"
#include "sdk/shm_probe.h"
#include "sdk/shm_writer.h"
#include "spdiag/shm_layout.h"
#include "spdiag/perf_config.h"
#include "spdiag/log_macros.h"
#include "runtime/plugin_types.h"
#include "runtime/shm_reader.h"
#include "runtime/plugins/custom_perfpoint/custom_perfpoint.h"
#ifdef SPDIAG_USES_BPF
#include "runtime/plugins/ob_point/ob_bpf_context.h"
#endif
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
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>

namespace SpDiag {

namespace {

bool ProcessIsAlive(int32_t pid) {
    if (pid <= 0) return false;
    if (::kill(pid, 0) == 0) return true;
    return errno == EPERM;
}

std::chrono::milliseconds DumpRequestTimeout(uint32_t capacity) {
    const uint64_t timeout = std::max<uint64_t>(
        3000, std::min<uint64_t>(60000, 2000 + 6ULL * capacity));
    return std::chrono::milliseconds(timeout);
}

void PrintShmOperationError(const char* action, const std::string& shmName,
                            int errorCode) {
    std::cerr << "spdiag: failed to " << action << " SHM " << shmName
              << ": " << std::strerror(errorCode);
    if (errorCode == EACCES || errorCode == EPERM) {
        std::cerr << "; run the command as the SHM owner or root";
    }
    std::cerr << std::endl;
}

bool CheckShmExistence(const std::string& shmName, bool& exists) {
    int errorCode = 0;
    const auto state = Platform::ShmCheckExistence(shmName, &errorCode);
    if (state == ShmExistence::ERROR) {
        PrintShmOperationError("inspect", shmName, errorCode);
        return false;
    }
    exists = state == ShmExistence::PRESENT;
    return true;
}

std::string TrimCopy(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}

bool ReadFileFirstLine(const char* path, std::string& line) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }
    std::getline(in, line);
    line = TrimCopy(line);
    return !line.empty();
}

uint32_t CountCpuSet(const std::string& cpuset) {
    uint64_t count = 0;
    std::stringstream ss(cpuset);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token = TrimCopy(token);
        if (token.empty()) {
            continue;
        }

        size_t dash = token.find('-');
        try {
            if (dash == std::string::npos) {
                std::stoull(token);
                ++count;
            } else {
                uint64_t start = std::stoull(token.substr(0, dash));
                uint64_t end = std::stoull(token.substr(dash + 1));
                if (end >= start) {
                    count += end - start + 1;
                }
            }
        } catch (const std::exception& e) {
            SPDIAG_LOG_ERROR(LogModule::MANAGER, "Parse cpu list failed: '%s', exception: %s", 
                             token.c_str(), e.what());
            return 0;
        }
    }

    return static_cast<uint32_t>(std::min<uint64_t>(count, std::numeric_limits<uint32_t>::max()));
}

uint32_t ReadCgroupCpuQuotaCores() {
    std::string line;
    if (ReadFileFirstLine("/sys/fs/cgroup/cpu.max", line)) {
        std::istringstream iss(line);
        std::string quotaText;
        uint64_t period = 0;
        iss >> quotaText >> period;
        if (quotaText != "max" && period > 0) {
            try {
                uint64_t quota = std::stoull(quotaText);
                return static_cast<uint32_t>(std::max<uint64_t>(1, (quota + period - 1) / period));
            } catch (const std::exception& e) {
                SPDIAG_LOG_ERROR(LogModule::MANAGER, "Parse cpu.max failed: '%s', exception: %s", 
                                 quotaText.c_str(), e.what());
            }
        }
    }

    std::string quotaLine;
    std::string periodLine;
    if (ReadFileFirstLine("/sys/fs/cgroup/cpu/cpu.cfs_quota_us", quotaLine) &&
        ReadFileFirstLine("/sys/fs/cgroup/cpu/cpu.cfs_period_us", periodLine)) {
        try {
            int64_t quota = std::stoll(quotaLine);
            uint64_t period = std::stoull(periodLine);
            if (quota > 0 && period > 0) {
                uint64_t quotaUnsigned = static_cast<uint64_t>(quota);
                return static_cast<uint32_t>(std::max<uint64_t>(1, (quotaUnsigned + period - 1) / period));
            }
        } catch (const std::exception& e) {
            SPDIAG_LOG_ERROR(LogModule::MANAGER, "Parse cpu.cfs_quota_us/cpu.cfs_period_us failed: quota='%s', period='%s', exception: %s", 
                             quotaLine.c_str(), periodLine.c_str(), e.what());
        }
    }

    return 0;
}

uint32_t ReadCgroupCpuSetCores() {
    const char* paths[] = {
        "/sys/fs/cgroup/cpuset.cpus.effective",
        "/sys/fs/cgroup/cpuset.cpus",
        "/sys/fs/cgroup/cpuset/cpuset.cpus.effective",
        "/sys/fs/cgroup/cpuset/cpuset.cpus",
    };

    for (const char* path : paths) {
        std::string line;
        if (ReadFileFirstLine(path, line)) {
            uint32_t count = CountCpuSet(line);
            if (count > 0) {
                return count;
            }
        }
    }

    return 0;
}

uint32_t DetectAvailableCpuCores() {
    uint32_t quotaCores = ReadCgroupCpuQuotaCores();
    uint32_t cpusetCores = ReadCgroupCpuSetCores();
    uint32_t hardwareCores = std::thread::hardware_concurrency();
    if (hardwareCores == 0) {
        hardwareCores = 4;
    }

    uint32_t detected = hardwareCores;
    if (quotaCores > 0) {
        detected = std::min(detected, quotaCores);
    }
    if (cpusetCores > 0) {
        detected = std::min(detected, cpusetCores);
    }

    return (detected > 0) ? detected : 4;
}

}

SpDiagManager::SpDiagManager()
    : pluginManager_(processor_) {
}

void SpDiagManager::RegisterPluginFactory(const std::string& name, PluginFactory factory) {
    pluginManager_.RegisterFactory(name, std::move(factory));
}

bool SpDiagManager::StartObCollect(const Cli::CliConfig& config) {
#ifdef SPDIAG_HAS_OB
    switch (config.command) {
        case Cli::Command::MEMSTAT:
#ifdef SPDIAG_ENABLE_OB_MEMORY
            obBpfContext_ = std::make_shared<ObBpfContext>(config.obTargetPid, config.obLibcPath);
            if (!obBpfContext_->Load()) {
                SPDIAG_LOG_ERROR(LogModule::MANAGER, "ObBpfContext Load failed for pid %d", config.obTargetPid);
                obBpfContext_.reset();
                return false;
            }
            return EnsureMemstatPlugin(config);
#else
            SPDIAG_LOG_ERROR(LogModule::MANAGER, "memstat not compiled (ENABLE_OB_MEMORY=OFF)");
            return false;
#endif
        case Cli::Command::CACHESTAT:
#ifdef SPDIAG_ENABLE_OB_CACHE
            return EnsureCachestatPlugin(config);
#else
            SPDIAG_LOG_ERROR(LogModule::MANAGER, "cachestat not compiled (ENABLE_OB_CACHE=OFF)");
            return false;
#endif
        case Cli::Command::MEMPOINT:
#ifdef SPDIAG_ENABLE_MEMPOINT
            obBpfContext_ = std::make_shared<ObBpfContext>(config.obTargetPid, config.obLibcPath);
            if (!obBpfContext_->Load()) {
                SPDIAG_LOG_ERROR(LogModule::MANAGER, "ObBpfContext Load failed for pid %d", config.obTargetPid);
                obBpfContext_.reset();
                return false;
            }
            return EnsureMempointPlugin(config);
#else
            SPDIAG_LOG_ERROR(LogModule::MANAGER, "mempoint not compiled (ENABLE_MEMPOINT=OFF)");
            return false;
#endif
        default:
            return false;
    }
#else
    (void)config;
    SPDIAG_LOG_ERROR(LogModule::MANAGER, "OB observation features are not compiled. Rebuild with ENABLE_OB_MEMORY, ENABLE_OB_CACHE, or ENABLE_MEMPOINT.");
    return false;
#endif
}

std::vector<ProcessorOutput> SpDiagManager::CollectObData() {
    return processor_.GetOutput();
}

void SpDiagManager::StopObCollect() {
#ifdef SPDIAG_HAS_OB
    // Tear down whichever OB plugin(s) are registered; StopPlugin/DestroyPlugin
    // are no-ops on names that were never registered, so covering all three is safe.
    pluginManager_.StopPlugin("memstat");
    pluginManager_.DestroyPlugin("memstat");
    pluginManager_.StopPlugin("cachestat");
    pluginManager_.DestroyPlugin("cachestat");
    pluginManager_.StopPlugin("mempoint");
    pluginManager_.DestroyPlugin("mempoint");
#ifdef SPDIAG_USES_BPF
    obBpfContext_.reset();
#endif
#endif
}

CollectResult SpDiagManager::CollectAndAggregate(const Cli::CliConfig& config) {
    CollectResult result;

    if (!EnsureCustomPlugin(config)) {
        return result;
    }

    ShmReader reader;
    if (!reader.Open(config.shmName)) {
        return result;
    }

    const ShmHeader& hdr = reader.Header();
    result.numCores = hdr.numCores;
    result.maxSlots = hdr.maxSlots;
    result.perCoreTopNCapacity = hdr.perCoreTopNCapacity;
#ifdef SPDIAG_ENABLE_PERFLOG
    result.perflogEnabled = hdr.perflogEnabled;
    result.perflogBootCounter = hdr.perflogBootCounter;
    result.perflogBootSystemNs = hdr.perflogBootSystemNs;
    result.perflogTimerFreq = hdr.perflogTimerFreq;
#endif

    const ProgramEntry* rawPrograms = reader.ReadProgramRegistry();
    result.programs.assign(rawPrograms, rawPrograms + hdr.registeredPrograms);

    const char (*rawKeyNames)[nameLen] = reader.ReadKeyNames();
    result.keyPointNameData.assign(
        reinterpret_cast<const char*>(rawKeyNames),
        reinterpret_cast<const char*>(rawKeyNames) + static_cast<size_t>(hdr.maxSlots) * nameLen);

    const char (*rawKeyModNames)[nameLen] = reader.ReadKeyModuleNames();
    result.keyModuleNameData.assign(
        reinterpret_cast<const char*>(rawKeyModNames),
        reinterpret_cast<const char*>(rawKeyModNames) + static_cast<size_t>(hdr.maxSlots) * nameLen);

    result.globalAccumCount = hdr.globalAccumCount;
    bool snapshotConsistent = reader.SnapshotShards(result.shards, &result.globalAccumData);
    if (!snapshotConsistent) {
        reader.Close();
        return result;
    }

    reader.Close();

    Aggregator aggregator;
    aggregator.Aggregate(
        result.programs.data(), static_cast<uint32_t>(result.programs.size()),
        reinterpret_cast<const char(*)[nameLen]>(result.keyPointNameData.data()),
        reinterpret_cast<const char(*)[nameLen]>(result.keyModuleNameData.data()),
        result.shards, result.numCores,
        result.globalAccumData.empty()
            ? nullptr : result.globalAccumData.data(),
        result.globalAccumCount);
    result.aggregated = aggregator.Results();

    PersistentAggr persistentAggr(config.dataDir);
    result.delta = persistentAggr.Update(result.aggregated);

    DumpRecorder recorder(config.historySize);
    std::string historyPath = config.dataDir + "/history.bin";
    recorder.LoadFromFile(historyPath);

    DumpRecord record;
    record.timestampNs = Platform::GetTimestampNs();
    for (const auto& stat : result.delta) {
        DumpStat ds;
        ds.globalSlotIndex = stat.globalSlotIndex;
        ds.programName = stat.programName;
        ds.moduleName = stat.moduleName;
        ds.pointName = stat.pointName;
        ds.level = stat.level;
        ds.deltaTickCount = stat.totalTickCount;
        ds.deltaGoodCount = stat.totalGoodCount;
        ds.deltaBadCount = stat.totalBadCount;
        ds.deltaNotCount = stat.totalNotCount;
        ds.deltaTimeNs = stat.totalTimeNs;
        ds.minTimeNs = stat.globalMinTimeNs;
        ds.maxTimeNs = stat.globalMaxTimeNs;
#ifdef SPDIAG_ENABLE_PERCENTILE
        ds.topN = stat.globalTopN;
        ds.p99Ns = stat.p99Ns;
        ds.p999Ns = stat.p999Ns;
        ds.p9999Ns = stat.p9999Ns;
        ds.hasP99 = stat.hasP99;
        ds.hasP999 = stat.hasP999;
        ds.hasP9999 = stat.hasP9999;
#endif
        record.stats.push_back(ds);
    }
    recorder.Record(record);
    recorder.SaveToFile(historyPath);

    return result;
}

bool SpDiagManager::RequestFreshDump(const Cli::CliConfig& config) {
    const auto probe = ProbeShmHeader(config.shmName);
    if (!probe.valid) return false;
    ShmWriter writer;
    if (!writer.Open(config.shmName, probe.totalSize)) return false;
    ShmHeader* header = writer.Header();
    ProgramEntry* registry = reinterpret_cast<ProgramEntry*>(
        reinterpret_cast<uint8_t*>(header) + header->registryOffset);
    std::vector<uint32_t> targets;
    for (uint32_t i = 0; i < header->registeredPrograms; ++i) {
        if (registry[i].active && ProcessIsAlive(registry[i].pid)) {
            targets.push_back(i);
        }
    }
    const uint64_t request = header->dumpRequestGeneration.fetch_add(
        1, std::memory_order_acq_rel) + 1;
    const auto deadline = std::chrono::steady_clock::now() +
                          DumpRequestTimeout(header->perCoreTopNCapacity);
    while (true) {
        bool complete = true;
        for (uint32_t index : targets) {
            if (!ProcessIsAlive(registry[index].pid)) continue;
            if (__atomic_load_n(&registry[index].dumpAckGeneration,
                                __ATOMIC_ACQUIRE) < request) {
                complete = false;
                break;
            }
        }
        if (complete) return true;
        if (std::chrono::steady_clock::now() >= deadline) {
            std::cerr << "spdiag: timed out waiting for fresh dump request "
                      << request << std::endl;
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

std::vector<DumpRecord> SpDiagManager::QueryHistory(const Cli::CliConfig& config) {
    DumpRecorder recorder(config.historySize);
    std::string historyPath = config.dataDir + "/history.bin";

    if (!recorder.LoadFromFile(historyPath) || recorder.Count() == 0) {
        return {};
    }

    std::vector<DumpRecord> records;
    for (size_t i = 0; i < recorder.Count(); ++i) {
        records.push_back(recorder.GetRecord(i));
    }
    return records;
}

bool SpDiagManager::ClearData(const Cli::CliConfig& config) {
    if (!processor_.ClearSharedMemory(config.shmName)) {
        SPDIAG_LOG_ERROR(LogModule::MANAGER, "failed to clear shared memory");
        return false;
    }

    if (config.clearHistory) {
        std::cout << "Clear history files? [y/n] ";
        std::string response;
        std::getline(std::cin, response);
        if (response != "y" && response != "Y") {
            std::cout << "spdiag: operation cancelled" << std::endl;
            return false;
        }

        std::string aggrPath = config.dataDir + "/aggr.bin";
        std::string snapshotPath = config.dataDir + "/snapshot.bin";
        std::string historyPath = config.dataDir + "/history.bin";

        std::remove(aggrPath.c_str());
        std::remove(snapshotPath.c_str());
        std::remove(historyPath.c_str());

        std::cout << "spdiag: history files cleared" << std::endl;
    }

    std::cout << "spdiag: performance data cleared" << std::endl;
    return true;
}

bool SpDiagManager::CheckShmAlive(const std::string& shmName) const {
    return ProbeShmHeader(shmName).valid;
}

bool SpDiagManager::CreateShm(const Cli::CliConfig& config) {
    uint32_t numCores = (config.numCores > 0) ? config.numCores : DetectAvailableCpuCores();

    uint32_t cfgMaxSlots = (config.maxSlots > 0) ? config.maxSlots : PerfConfig().maxSlots;
    uint32_t cfgMaxPrograms = (config.maxPrograms > 0) ? config.maxPrograms : maxProgramsDefault;
    ShmLayoutDescriptor layout;
    if (!ComputeShmLayout(cfgMaxSlots, cfgMaxPrograms, numCores, layout,
                          config.perCoreTopN)) {
        SPDIAG_LOG_ERROR(LogModule::MANAGER,
                         "invalid or overflowing shared-memory layout");
        return false;
    }

    ShmWriter writer;
    if (!writer.Create(config.shmName, layout.totalSize)) {
        SPDIAG_LOG_ERROR(LogModule::MANAGER, "Failed to create shared memory: %s", config.shmName.c_str());
        return false;
    }

    ShmHeader hdr{};
    hdr.magic = shmMagic;
    hdr.version = 3;
    hdr.mode = 0;
    hdr.levelGate = (config.levelGate > 0) ? config.levelGate : 4;
    hdr.reserved[0] = shmLayoutAbiRevision;
    hdr.reserved[1] = CurrentShmFeatureMask();
    hdr.maxSlots = cfgMaxSlots;
    hdr.maxPrograms = cfgMaxPrograms;
    hdr.registeredPrograms = 0;
    hdr.numCores = numCores;
    hdr.perCoreTopNCapacity = config.perCoreTopN;
    hdr.dumpIntervalMs = config.dumpIntervalMs;
    {
        static uint32_t s_gen = 0;
        hdr.generation = ++s_gen;     // 每次创建递增，SDK 可通过对比检测 SHM 重启
    }
    hdr.dumpSeqNo.store(0, std::memory_order_relaxed);
    hdr.dumpRequestGeneration.store(0, std::memory_order_relaxed);
    hdr.lastDumpTimestampNs = 0;
    hdr.shmTotalSize = layout.totalSize;
    hdr.registryOffset = layout.registryOffset;
    hdr.keyNamesOffset = layout.keyNamesOffset;
    hdr.keyModuleNamesOffset = layout.keyModuleNamesOffset;
    hdr.activeBitmapOffset = layout.activeBitmapOffset;
    hdr.shardsOffset = layout.shardsOffset;
    hdr.globalAccumOffset = layout.globalAccumOffset;
    hdr.globalAccumCount = cfgMaxSlots;
#ifdef SPDIAG_ENABLE_PERFLOG
    hdr.perflogEnabled = config.perflogStart ? 1 : 0;
    if (config.perflogStart) {
        uint64_t cntvct, cntfrq;
        __asm__ volatile("mrs %0, cntvct_el0" : "=r"(cntvct));
        __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(cntfrq));
        hdr.perflogBootCounter = cntvct;
        hdr.perflogTimerFreq = cntfrq;
        hdr.perflogBootSystemNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    } else {
        hdr.perflogBootCounter = 0;
        hdr.perflogBootSystemNs = 0;
        hdr.perflogTimerFreq = 0;
    }
#endif
    hdr.spinlock.clear();
    writer.WriteHeader(hdr);

    writer.Release();
    return true;
}

bool SpDiagManager::DestroyShm(const std::string& shmName, int* errorCode) {
    return Platform::ShmDestroy(shmName, errorCode);
}

bool SpDiagManager::RecreateShm(const Cli::CliConfig& config,
                                bool startCommand) {
    int lockError = 0;
    auto lifecycle = FileLockGuard::AcquireFor(
        MakeLockName("lifecycle", config.shmName),
        std::chrono::seconds(5), &lockError);
    if (!lifecycle) {
        std::cerr << "spdiag: lifecycle lock unavailable (error "
                  << lockError << ")" << std::endl;
        return false;
    }

    if (startCommand) {
        bool exists = false;
        if (!CheckShmExistence(config.shmName, exists)) {
            return false;
        }
        if (exists) {
            const auto probe = ProbeShmHeader(config.shmName);
            if (!probe.valid) {
                std::cerr
                    << "spdiag: existing SHM cannot be used safely; stop "
                       "Mooncake and run 'spdiag restart'"
                    << std::endl;
                return false;
            }
            int accessError = 0;
            if (!Platform::ShmCheckReadWrite(config.shmName, &accessError)) {
                PrintShmOperationError("open read-write", config.shmName,
                                       accessError);
                return false;
            }
            std::cout << "spdiag already running (SHM: " << config.shmName
                      << ")" << std::endl;
            return true;
        }
    } else {
        std::cerr << "spdiag: warning: restart is a repair operation that "
                     "force-recreates the shared SHM; stop Mooncake first "
                     "because this command does not stop business processes"
                  << std::endl;
    }

    int destroyError = 0;
    if (!DestroyShm(config.shmName, &destroyError)) {
        PrintShmOperationError("remove", config.shmName, destroyError);
        return false;
    }
    if (!CreateShm(config)) {
        return false;
    }
    auto probe = ProbeShmHeader(config.shmName);
    if (!probe.valid) {
        (void)DestroyShm(config.shmName);
        std::cerr << "spdiag: created SHM failed verification" << std::endl;
        return false;
    }

    std::cout << "spdiag " << (startCommand ? "started" : "restarted")
              << " (SHM: " << config.shmName << ")" << std::endl;
    return true;
}

bool SpDiagManager::StartShm(const Cli::CliConfig& config) {
    return RecreateShm(config, true);
}

bool SpDiagManager::StopShm(const Cli::CliConfig& config) {
    int lockError = 0;
    auto lifecycle = FileLockGuard::AcquireFor(
        MakeLockName("lifecycle", config.shmName),
        std::chrono::seconds(5), &lockError);
    if (!lifecycle) {
        std::cerr << "spdiag: lifecycle lock unavailable (error "
                  << lockError << ")" << std::endl;
        return false;
    }

    int inspectError = 0;
    const auto existence =
        Platform::ShmCheckExistence(config.shmName, &inspectError);
    if (existence == ShmExistence::PRESENT) {
        std::cerr << "spdiag: warning: stop disables shared SpDiag "
                     "observation for all users; business processes are not "
                     "stopped" << std::endl;
    } else if (existence == ShmExistence::ERROR) {
        std::cerr << "spdiag: warning: unable to inspect SHM "
                  << config.shmName << ": " << std::strerror(inspectError)
                  << "; attempting unlink for recovery" << std::endl;
    }
    int destroyError = 0;
    if (!DestroyShm(config.shmName, &destroyError)) {
        PrintShmOperationError("remove", config.shmName, destroyError);
        return false;
    }
    std::cout << "spdiag stopped" << std::endl;
    return true;
}

bool SpDiagManager::ShmStatus(const Cli::CliConfig& config) {
    if (CheckShmAlive(config.shmName)) {
        std::cout << "spdiag is running (SHM: " << config.shmName << ")" << std::endl;
        return true;
    }
    std::cout << "spdiag is not running" << std::endl;
    return false;
}

bool SpDiagManager::RestartShm(const Cli::CliConfig& config) {
    return RecreateShm(config, false);
}

bool SpDiagManager::EnsureCustomPlugin(const Cli::CliConfig& config) {
    CustomPerfpointConfig pluginConfig;
    pluginConfig.shmName = config.shmName;
    pluginConfig.dataSink = pluginManager_.GetPluginSink("custom_perfpoint");
    pluginConfig.watchMode = false;
    pluginConfig.watchIntervalMs = config.watchIntervalMs;
    pluginConfig.strictSnapshot = config.strictSnapshot;

    if (!pluginManager_.EnsurePlugin("custom_perfpoint", std::make_shared<CustomPerfpointConfig>(pluginConfig))) {
        return false;
    }

    pluginConfig.dataSink = pluginManager_.GetPluginSink("custom_perfpoint");
    return true;
}

#if defined(SPDIAG_HAS_OB)

bool SpDiagManager::EnsureMemstatPlugin(const Cli::CliConfig& config) {
#if !defined(SPDIAG_HAS_OB) || !defined(SPDIAG_ENABLE_OB_MEMORY)
    (void)config;
    return false;
#else
    SpDiagHealthInfo health;
    if (!pluginManager_.GetPluginHealth("memstat", health)) {
        if (!pluginManager_.RegisterPlugin(std::make_unique<SpDiagMemstatPlugin>())) return false;
    }
    MemstatConfig cfg;
    cfg.targetPid = config.obTargetPid;
    cfg.libcPath = config.obLibcPath;
    cfg.pollIntervalMs = config.obIntervalMs;
    cfg.bpfContext = obBpfContext_.get();
    cfg.dataSink = pluginManager_.GetPluginSink("memstat");
    if (!pluginManager_.InitPlugin("memstat", std::make_shared<MemstatConfig>(cfg))) {
        if (!pluginManager_.StopPlugin("memstat")) return false;
        if (!pluginManager_.InitPlugin("memstat", std::make_shared<MemstatConfig>(cfg))) return false;
    }
    return pluginManager_.StartPlugin("memstat");
#endif
}

bool SpDiagManager::EnsureCachestatPlugin(const Cli::CliConfig& config) {
#if !defined(SPDIAG_HAS_OB) || !defined(SPDIAG_ENABLE_OB_CACHE)
    (void)config;
    return false;
#else
    SpDiagHealthInfo health;
    if (!pluginManager_.GetPluginHealth("cachestat", health)) {
        if (!pluginManager_.RegisterPlugin(std::make_unique<SpDiagCachestatPlugin>())) return false;
    }
    CachestatConfig cfg;
    cfg.targetPid = config.obTargetPid;
    cfg.cacheLevels = config.obCacheLevels;
    cfg.pollIntervalMs = config.obIntervalMs;
    cfg.dataSink = pluginManager_.GetPluginSink("cachestat");
    if (!pluginManager_.InitPlugin("cachestat", std::make_shared<CachestatConfig>(cfg))) {
        if (!pluginManager_.StopPlugin("cachestat")) return false;
        if (!pluginManager_.InitPlugin("cachestat", std::make_shared<CachestatConfig>(cfg))) return false;
    }
    return pluginManager_.StartPlugin("cachestat");
#endif
}

bool SpDiagManager::EnsureMempointPlugin(const Cli::CliConfig& config) {
#if !defined(SPDIAG_HAS_OB) || !defined(SPDIAG_ENABLE_MEMPOINT)
    (void)config;
    return false;
#else
    SpDiagHealthInfo health;
    if (!pluginManager_.GetPluginHealth("mempoint", health)) {
        if (!pluginManager_.RegisterPlugin(std::make_unique<SpDiagMempointPlugin>())) return false;
    }
    MempointConfig cfg;
    cfg.targetPid = config.obTargetPid;
    cfg.binaryPath = "/proc/" + std::to_string(config.obTargetPid) + "/exe";
    cfg.pollIntervalMs = config.obIntervalMs;
    cfg.bpfContext = obBpfContext_.get();
    cfg.dataSink = pluginManager_.GetPluginSink("mempoint");
    if (!pluginManager_.InitPlugin("mempoint", std::make_shared<MempointConfig>(cfg))) {
        if (!pluginManager_.StopPlugin("mempoint")) return false;
        if (!pluginManager_.InitPlugin("mempoint", std::make_shared<MempointConfig>(cfg))) return false;
    }
    return pluginManager_.StartPlugin("mempoint");
#endif
}

#endif  // defined(SPDIAG_HAS_OB)

}
