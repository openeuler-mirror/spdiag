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
#include <string>
#include <shared_mutex>
#include <memory>
#include <vector>
#include <atomic>
#include <thread>
#include "spdiag/perf_config.h"
#include "spdiag/perf_level.h"
#include "spdiag/perf_shard.h"
#include "spdiag/shm_layout.h"

namespace SpDiag {

namespace detail {
    extern uint32_t g_autoSlotCount;
    extern const char* g_autoProgramName;
    extern const char** g_autoPointNames;
    extern const char** g_autoModuleNames;

    inline uint32_t& AutoSlotCount() { return g_autoSlotCount; }
    inline const char*& AutoProgramName() { return g_autoProgramName; }
    inline const char**& AutoPointNames() { return g_autoPointNames; }
    inline const char**& AutoModuleNames() { return g_autoModuleNames; }
}

class ShmWriter;
class PerfTimer;

class PerfManager {
public:
    static PerfManager& Instance();

    void Init(const PerfConfig& config);
    void Shutdown();

    void EnsureInitialized();
    bool IsInitialized() const;
    bool GetProbeDone() const { return probeDone_.load(std::memory_order_acquire); }  // 临时：验证用，PR 合入前删除

    uint32_t FastResolveSlot(uint32_t localIndex) const;
    bool IsLevelEnabled(PerfLevel level) const;
#ifdef SPDIAG_ENABLE_PERFLOG
    bool IsPerfLogEnabled() const;
#endif

    LocalPerfShard& GetShard();

    RunMode Mode() const;
    uint8_t LevelGate() const;
    uint32_t NumCores() const;
    PerfShardManager& GetShardManager();

private:
    PerfManager();
    ~PerfManager();

    void LoadDefFile(const char* defPath);
    bool ConnectToExistingShm(uint32_t dumpIntervalHint);

    void StartShmWatcher();

    RunMode     mode_ = RunMode::DEBUG;
    uint8_t     levelGate_ = 2;
    uint32_t    numCores_ = 0;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> probeDone_{false};
    std::atomic<bool> watcherStarted_{false};
    std::atomic<bool> shmWatcherStop_{false};
    std::unique_ptr<std::thread> shmWatcher_;
    uint32_t generation_ = 0;
#ifdef SPDIAG_ENABLE_PERFLOG
    std::atomic<bool> perflogEnabled_{false};
#endif

    uint32_t baseOffset_ = 0;
    uint32_t slotCount_ = 0;
    uint32_t programIndex_ = 0;
    std::string programName_;
    std::mutex registryMutex_;
    std::mutex initMutex_;

    PerfShardManager shardManager_;

    std::unique_ptr<ShmWriter> shmWriter_;
    std::unique_ptr<PerfTimer> timer_;

    std::string shmName_ = "/spdiag_shm_default";
    std::string defFilePath_;
    std::vector<std::string> keyModuleNames_;
    std::vector<std::string> keyPointNames_;

    struct ConnectParams {
        const char* programName;
        uint32_t dumpIntervalMs;
    };
    bool ConnectAndRegister(const ConnectParams& params);
};

}
