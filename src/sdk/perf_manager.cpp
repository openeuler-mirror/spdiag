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

#include "spdiag/perf_manager.h"
#include "spdiag/perf_key_defined.h"
#include "spdiag/log_macros.h"
#include "shm_writer.h"
#include "perf_timer.h"
#include "platform.h"
#include "shm_probe.h"
#include "global_perf_point_factory.h"
#include "registry_file_guard.h"
#include <chrono>
#include <thread>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <iostream>
#include <unistd.h>
#include <sys/inotify.h>
#include <poll.h>


namespace SpDiag {

namespace {

constexpr auto registryLockTimeout = std::chrono::milliseconds(30000);

void ResolveAndWriteSlotName(
    char* keyNames, char* keyModuleNames,
    uint32_t slot, uint32_t arrIdx,
    const char** autoPointNames, const char** autoModuleNames) {
    const char* pointName = (autoPointNames) ? autoPointNames[arrIdx] : perfKeyDefinedPointNames[arrIdx];
    const char* moduleName = (autoModuleNames) ? autoModuleNames[arrIdx] : perfKeyDefinedModules[arrIdx];
    std::strncpy(keyNames + slot * nameLen, pointName, nameLen - 1);
    keyNames[slot * nameLen + nameLen - 1] = '\0';
    std::strncpy(keyModuleNames + slot * nameLen, moduleName, nameLen - 1);
    keyModuleNames[slot * nameLen + nameLen - 1] = '\0';
}

const char* ResolvePointName(uint32_t arrIdx, const char** autoPointNames) {
    return (autoPointNames) ? autoPointNames[arrIdx] : perfKeyDefinedPointNames[arrIdx];
}

const char* ResolveModuleName(uint32_t arrIdx, const char** autoModuleNames) {
    return (autoModuleNames) ? autoModuleNames[arrIdx] : perfKeyDefinedModules[arrIdx];
}

}

namespace detail {
    uint32_t g_autoSlotCount = 0;
    const char* g_autoProgramName = nullptr;
    const char** g_autoPointNames = nullptr;
    const char** g_autoModuleNames = nullptr;
}

PerfManager::PerfManager() {
}

PerfManager::~PerfManager() {
    Shutdown();
}

PerfManager& PerfManager::Instance() {
    static PerfManager instance;
    return instance;
}

void PerfManager::Init(const PerfConfig& config) {
    if (initialized_.load(std::memory_order_acquire)) {
        return;
    }

    std::lock_guard<std::mutex> lock(initMutex_);
    if (initialized_.load(std::memory_order_acquire)) {
        return;
    }

    shmName_ = config.shmName;
    mode_ = config.mode;
    programName_ = config.programName;

    if (config.levelGate == 0) {
        levelGate_ = (mode_ == RunMode::RELEASE) ? 1 : 2;
    } else {
        levelGate_ = config.levelGate;
    }

    uint32_t dumpInterval = config.dumpIntervalMs;
    if (dumpInterval == 0) {
        dumpInterval = (mode_ == RunMode::RELEASE) ? 5000 : 1000;
    }

    numCores_ = std::thread::hardware_concurrency();
    if (numCores_ == 0) {
        numCores_ = 4;
    }

    slotCount_ = (config.slotCount > 0) ? config.slotCount
                : static_cast<uint32_t>(PerfKeyDefined::PERF_KEY_DEFINED_COUNT);

    if (config.defFilePath) {
        defFilePath_ = config.defFilePath;
        LoadDefFile(config.defFilePath);
    }

    if (!ConnectToExistingShm(dumpInterval)) {
        return;
    }

    initialized_.store(true, std::memory_order_release);
    probeDone_.store(true, std::memory_order_release);
}

void PerfManager::EnsureInitialized() {
    if (probeDone_.load(std::memory_order_acquire)) {
        return;
    }

    std::lock_guard<std::mutex> lock(initMutex_);
    if (probeDone_.load(std::memory_order_acquire)) {
        return;
    }

    if (slotCount_ == 0) {
        uint32_t autoCount = detail::AutoSlotCount();
        slotCount_ = (autoCount > 0) ? autoCount
                    : static_cast<uint32_t>(PerfKeyDefined::PERF_KEY_DEFINED_COUNT);
    }

    shmWriter_ = std::make_unique<ShmWriter>();

    ShmHandle probeHandle = Platform::ShmOpenReadOnly(shmName_, sizeof(ShmHeader));
    if (probeHandle.baseAddr == nullptr) {
        shmWriter_.reset();
        probeDone_.store(true, std::memory_order_release);
        StartShmWatcher();
        return;
    }

    const ShmHeader* probeHdr = reinterpret_cast<const ShmHeader*>(probeHandle.baseAddr);
    if (probeHdr->magic != shmMagic || probeHdr->shmTotalSize == 0) {
        std::cerr << "spdiag: invalid shared memory. Please run 'spdiag start' first." << std::endl;
        Platform::ShmClose(probeHandle);
        shmWriter_.reset();
        probeDone_.store(true, std::memory_order_release);
        StartShmWatcher();
        return;
    }

    size_t existingSize = probeHdr->shmTotalSize;
    Platform::ShmClose(probeHandle);

    if (!shmWriter_->Open(shmName_, existingSize)) {
        std::cerr << "spdiag: failed to connect to shared memory." << std::endl;
        shmWriter_.reset();
        probeDone_.store(true, std::memory_order_release);
        StartShmWatcher();
        return;
    }

    const ShmHeader* existingHdr = shmWriter_->Header();
    numCores_ = existingHdr->numCores;
    levelGate_ = existingHdr->levelGate;
    mode_ = static_cast<RunMode>(existingHdr->mode);
    generation_ = existingHdr->generation;
#ifdef SPDIAG_ENABLE_PERFLOG
    perflogEnabled_.store(existingHdr->perflogEnabled != 0, std::memory_order_relaxed);
#endif

    shardManager_ = PerfShardManager();
    shardManager_.Init(numCores_);

    if (programName_.empty()) {
        const char* autoName = detail::AutoProgramName();
        if (autoName) {
            programName_ = autoName;
        } else {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "proc_%d", getpid());

            programName_ = buf;
        }
    }

    if (!ConnectToExistingShm(0)) {
        SPDIAG_LOG_ERROR(LogModule::SDK, "failed to connect to shared memory. Please run 'spdiag start' first.");
        probeDone_.store(true, std::memory_order_release);
        return;
    }

    initialized_.store(true, std::memory_order_release);
    probeDone_.store(true, std::memory_order_release);
}

bool PerfManager::IsInitialized() const {
    return initialized_.load(std::memory_order_acquire);
}

bool PerfManager::ConnectToExistingShm(uint32_t dumpIntervalHint) {
    shmWriter_ = std::make_unique<ShmWriter>();

    auto probe = ProbeShmHeader(shmName_);
    if (!probe.valid) {
        shmWriter_.reset();
        return false;
    }

    if (!shmWriter_->Open(shmName_, probe.totalSize)) {
        shmWriter_.reset();
        return false;
    }

    const ShmHeader* existingHdr = shmWriter_->Header();

    numCores_ = existingHdr->numCores;
    levelGate_ = existingHdr->levelGate;
    mode_ = static_cast<RunMode>(existingHdr->mode);
    generation_ = existingHdr->generation;
#ifdef SPDIAG_ENABLE_PERFLOG
    perflogEnabled_.store(existingHdr->perflogEnabled != 0, std::memory_order_relaxed);
#endif

    shardManager_ = PerfShardManager();
    shardManager_.Init(numCores_);

    uint32_t dumpInterval = (dumpIntervalHint > 0) ? dumpIntervalHint : existingHdr->dumpIntervalMs;

    ConnectParams params;
    params.programName = programName_.c_str();
    params.dumpIntervalMs = dumpInterval;

    if (!ConnectAndRegister(params)) {
        return false;
    }

    return true;
}

void PerfManager::Shutdown() {
    shmWatcherStop_.store(true, std::memory_order_relaxed);
    if (shmWatcher_ && shmWatcher_->joinable()) {
        shmWatcher_->join();
        shmWatcher_.reset();
    }
    shmWatcherStop_.store(false, std::memory_order_relaxed);
    watcherStarted_.store(false, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(initMutex_);
        if (!initialized_.load(std::memory_order_acquire)) {
            return;
        }

        if (timer_) {
            timer_->Stop();
            timer_.reset();
        }

        if (shmWriter_) {
            shmWriter_->Destroy();
            shmWriter_.reset();
        }

        GlobalPerfPointFactory::Instance().Reset();

        // 先开门（允许重新探测），再宣告未就绪。
        // 反之会导致并发 EnsureInitialized 在 probeDone_ 归零前
        // 被旧 true 拦住，错过重新探测。
        probeDone_.store(false, std::memory_order_release);
        initialized_.store(false, std::memory_order_release);
    }
}

void PerfManager::StartShmWatcher() {
    bool expected = false;
    if (!watcherStarted_.compare_exchange_strong(expected, true)) {
        return;
    }

    shmWatcher_ = std::make_unique<std::thread>([this]() {
        int fd = inotify_init1(IN_NONBLOCK);
        if (fd < 0) {
            return;
        }
        int wd = inotify_add_watch(fd, "/dev/shm/", IN_CREATE | IN_DELETE);
        if (wd < 0) {
            close(fd);
            return;
        }

        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;

        while (!shmWatcherStop_.load(std::memory_order_relaxed)) {
            int ret = poll(&pfd, 1, 1000);  // 1s 超时
            if (ret < 0) break;
            if (ret == 0) continue;

            char buf[4096];
            ssize_t len = read(fd, buf, sizeof(buf));
            if (len <= 0) continue;

            const char* target = "spdiag_shm_default";
            for (const struct inotify_event* ev = reinterpret_cast<const struct inotify_event*>(buf);
                 reinterpret_cast<const char*>(ev) < buf + len;
                 ev = reinterpret_cast<const struct inotify_event*>(
                     reinterpret_cast<const char*>(ev) + sizeof(struct inotify_event) + ev->len)) {
                if (ev->len > 0 && ev->name[0] != '\0' && std::strcmp(ev->name, target) == 0) {
                    if (ev->mask & IN_CREATE) {
                        probeDone_.store(false, std::memory_order_release);
                    } else if (ev->mask & IN_DELETE) {
                        initialized_.store(false, std::memory_order_release);
                    }
                    // 不退出循环，持续监听后续事件
                }
            }
        }
        inotify_rm_watch(fd, wd);
        close(fd);
        // watcher 常驻监听创建/删除，直到 Shutdown 停止
    });

}

bool PerfManager::ConnectAndRegister(const ConnectParams& params) {
    ShmHeader* hdr = shmWriter_->Header();
    uint8_t* base = reinterpret_cast<uint8_t*>(hdr);

    int lockError = 0;
    auto registryGuard = RegistryFileGuard::AcquireFor(
        shmWriter_->Fd(), registryLockTimeout, &lockError);
    if (!registryGuard) {
        SPDIAG_LOG_ERROR(
            LogModule::SDK,
            "shared-memory registration lock failed after %lld ms: errno=%d (%s)",
            static_cast<long long>(registryLockTimeout.count()), lockError,
            std::strerror(lockError));
        shmWriter_.reset();
        return false;
    }

    ProgramEntry* registry = reinterpret_cast<ProgramEntry*>(base + hdr->registryOffset);

    uint32_t baseSlot = 0;
    int32_t pid = static_cast<int32_t>(getpid());

    bool reused = false;

    char* keyNames = reinterpret_cast<char*>(base + hdr->keyNamesOffset);
    char* keyModuleNames = reinterpret_cast<char*>(base + hdr->keyModuleNamesOffset);
    const char** autoPointNames = detail::AutoPointNames();
    const char** autoModuleNames = detail::AutoModuleNames();

    for (uint32_t i = 0; i < hdr->registeredPrograms; ++i) {
        if (registry[i].active && std::strncmp(registry[i].name, params.programName, 63) == 0) {
            baseSlot = registry[i].baseSlot;
            programIndex_ = i;
            reused = true;
            registry[i].pid = pid;

            // 对比名称，找出变化的 slot 并清零其 PerfStat
            uint32_t compareCount = (slotCount_ <= registry[i].slotCount) ? slotCount_ : registry[i].slotCount;
            for (uint32_t j = 1; j < compareCount; ++j) {
                uint32_t slot = baseSlot + j;
                uint32_t arrIdx = j - 1;
                const char* newPointName = ResolvePointName(arrIdx, autoPointNames);
                const char* newModuleName = ResolveModuleName(arrIdx, autoModuleNames);
                const char* oldPointName = keyNames + slot * nameLen;
                const char* oldModuleName = keyModuleNames + slot * nameLen;

                if (std::strncmp(oldPointName, newPointName, nameLen) != 0 ||
                    std::strncmp(oldModuleName, newModuleName, nameLen) != 0) {
                    for (uint32_t core = 0; core < hdr->numCores; ++core) {
                        void* shardPtr = shmWriter_->ShardPtr(programIndex_, core);
                        PerfStat* stats = ShmShardStats(shardPtr);
                        stats[slot].reset();
                    }
                }
            }

            uint32_t writeCount = (slotCount_ <= registry[i].slotCount) ? slotCount_ : registry[i].slotCount;
            for (uint32_t j = 1; j < writeCount; ++j) {
                ResolveAndWriteSlotName(keyNames, keyModuleNames, baseSlot + j, j - 1, autoPointNames, autoModuleNames);
            }

            if (slotCount_ > registry[i].slotCount &&
                baseSlot + slotCount_ <= hdr->maxSlots) {
                for (uint32_t j = registry[i].slotCount; j < slotCount_; ++j) {
                    ResolveAndWriteSlotName(keyNames, keyModuleNames, baseSlot + j, j - 1, autoPointNames, autoModuleNames);
                }
                registry[i].slotCount = slotCount_;
            }
            break;
        }
    }

    if (!reused) {
        for (uint32_t i = 0; i < hdr->registeredPrograms; ++i) {
            if (registry[i].active) {
                baseSlot += registry[i].slotCount;
            }
        }

        if (baseSlot + slotCount_ > hdr->maxSlots) {
            registryGuard.reset();
            shmWriter_.reset();
            return false;
        }

        programIndex_ = hdr->registeredPrograms;
        ProgramEntry& entry = registry[hdr->registeredPrograms];
        std::memset(&entry, 0, sizeof(ProgramEntry));
        std::strncpy(entry.name, params.programName, 63);
        entry.name[63] = '\0';
        entry.baseSlot = baseSlot;
        entry.slotCount = slotCount_;
        entry.numCores = numCores_;
        entry.shardOffset = 0;
        entry.pid = pid;
        entry.active = 1;

        for (uint32_t i = 1; i < slotCount_; ++i) {
            ResolveAndWriteSlotName(keyNames, keyModuleNames, baseSlot + i, i - 1, autoPointNames, autoModuleNames);
        }

        hdr->registeredPrograms++;
    }

    registryGuard.reset();

    baseOffset_ = baseSlot;

    Platform::CalibrateTimestamp();

    // 初始化全局 PerfPoint 工厂（进程内原子累加器数组），数量与 SHM 全局区一致。
    {
        uint32_t gCount = hdr->globalAccumCount;
        if (gCount == 0) gCount = hdr->maxSlots;   // 防御：v3 SHM 恒 > 0
        GlobalPerfPointFactory::Instance().Init(gCount);
    }

    timer_ = std::make_unique<PerfTimer>(*shmWriter_, shardManager_, params.dumpIntervalMs, baseOffset_, slotCount_, programIndex_);
    timer_->Start();
    return true;
}

uint32_t PerfManager::FastResolveSlot(uint32_t localIndex) const {
    return baseOffset_ + localIndex;
}

bool PerfManager::IsLevelEnabled(PerfLevel level) const {
    if (!initialized_.load(std::memory_order_acquire)) return false;
    return static_cast<uint8_t>(level) <= levelGate_;
}

#ifdef SPDIAG_ENABLE_PERFLOG
bool PerfManager::IsPerfLogEnabled() const {
    return perflogEnabled_.load(std::memory_order_relaxed);
}
#endif

LocalPerfShard& PerfManager::GetShard() {
    return shardManager_.GetShard();
}

RunMode PerfManager::Mode() const {
    return mode_;
}

uint8_t PerfManager::LevelGate() const {
    return levelGate_;
}

uint32_t PerfManager::NumCores() const {
    return numCores_;
}

PerfShardManager& PerfManager::GetShardManager() {
    return shardManager_;
}

void PerfManager::LoadDefFile(const char* defPath) {
    FILE* f = std::fopen(defPath, "r");
    if (!f) {
        SPDIAG_LOG_ERROR(LogModule::SDK, "failed to open def file: %s", defPath);
        return;
    }

    keyPointNames_.clear();
    keyModuleNames_.clear();

    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        char enumName[128], moduleName[128], pointName[128];
        if (std::sscanf(line, " PERF_KEY_DEF( %[^,], %[^,], %[^)] )",
                        enumName, moduleName, pointName) == 3) {
            // 去除引号和空白
            auto unquote = [](char* s) {
                char* start = s;
                while (*start == ' ' || *start == '\t') ++start;
                if (*start == '"') {
                    ++start;
                    char* end = start;
                    while (*end && *end != '"') ++end;
                    *end = '\0';
                }
                return std::string(start);
            };
            keyModuleNames_.push_back(unquote(moduleName));
            keyPointNames_.push_back(unquote(pointName));
        }
    }
    std::fclose(f);
}

}
