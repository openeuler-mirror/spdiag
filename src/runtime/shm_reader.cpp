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

#include "shm_reader.h"
#include <cstring>
#include <random>
#include <algorithm>
#include <thread>
#include <chrono>

namespace SpDiag {

ShmReader::~ShmReader() {
    Close();
}

bool ShmReader::Open(const std::string& shmName) {
    handle_ = Platform::ShmOpenReadOnly(shmName, sizeof(ShmHeader));
    if (handle_.baseAddr == nullptr) {
        return false;
    }

    const ShmHeader* hdr = reinterpret_cast<const ShmHeader*>(handle_.baseAddr);
    if (hdr->magic != shmMagic) {
        Platform::ShmClose(handle_);
        handle_.baseAddr = nullptr;
        return false;
    }

    size_t fullSize = hdr->shmTotalSize;
    Platform::ShmClose(handle_);

    handle_ = Platform::ShmOpenReadOnly(shmName, fullSize);
    if (handle_.baseAddr == nullptr) {
        return false;
    }

    opened_ = true;
    return true;
}

void ShmReader::Close() {
    if (opened_ && handle_.baseAddr != nullptr) {
        Platform::ShmClose(handle_);
        handle_.baseAddr = nullptr;
        opened_ = false;
    }
}

const ShmHeader& ShmReader::Header() const {
    return *reinterpret_cast<const ShmHeader*>(handle_.baseAddr);
}

const ProgramEntry* ShmReader::ReadProgramRegistry() const {
    const ShmHeader& hdr = Header();
    return reinterpret_cast<const ProgramEntry*>(
        reinterpret_cast<const uint8_t*>(handle_.baseAddr) + hdr.registryOffset);
}

const char (*ShmReader::ReadKeyNames() const)[nameLen] {
    const ShmHeader& hdr = Header();
    return reinterpret_cast<const char (*)[nameLen]>(
        reinterpret_cast<const uint8_t*>(handle_.baseAddr) + hdr.keyNamesOffset);
}

const char (*ShmReader::ReadKeyModuleNames() const)[nameLen] {
    const ShmHeader& hdr = Header();
    return reinterpret_cast<const char (*)[nameLen]>(
        reinterpret_cast<const uint8_t*>(handle_.baseAddr) + hdr.keyModuleNamesOffset);
}

const PerfStat* ShmReader::ReadGlobalAccum() const {
    const ShmHeader& hdr = Header();
    if (hdr.globalAccumCount == 0) {
        return nullptr;
    }
    return reinterpret_cast<const PerfStat*>(
        reinterpret_cast<const uint8_t*>(handle_.baseAddr) + hdr.globalAccumOffset);
}

bool ShmReader::SnapshotShards(ShardBuffer& out, std::vector<uint8_t>* globalOut) const {
    const ShmHeader& hdr = Header();
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> waitDist(0, 50);

    for (int retry = 0; retry < 20; ++retry) {
        // 阶段 1：等待 seqNo 变偶（轻量检查，不做 memcpy）
        uint64_t seq1 = 0;
        bool gotEven = false;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        int waitCount = 0;
        for (int wait = 0; wait < 200; ++wait) {
            seq1 = hdr.dumpSeqNo.load(std::memory_order_acquire);
            if (seq1 % 2 == 0) { gotEven = true; break; }
            // 每 16 次检查一次 deadline，对齐 writer 侧超时策略
            if ((++waitCount & 0xF) == 0 &&
                std::chrono::steady_clock::now() >= deadline) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(waitDist(gen)));
        }
        if (!gotEven) continue;

        // 阶段 2：偶数确认后才开始 memcpy
        std::atomic_thread_fence(std::memory_order_acquire);
        uint32_t numShards = hdr.numCores;
        out.resize(numShards, hdr.maxSlots);

        size_t shardSize = ShmShardSize(hdr.maxSlots);
        for (uint32_t i = 0; i < numShards; ++i) {
            const void* src = reinterpret_cast<const uint8_t*>(
                handle_.baseAddr) + hdr.shardsOffset + i * shardSize;
            std::memcpy(out.shardData(i), src, shardSize);
        }

        // 同窗口快照全局 PerfPoint 累加区（无 coreId 前缀，原始 PerfStat[] 字节）。
        if (globalOut != nullptr && hdr.globalAccumCount > 0) {
            size_t globalBytes = static_cast<size_t>(hdr.globalAccumCount) * sizeof(PerfStat);
            globalOut->resize(globalBytes);
            const void* gsrc = reinterpret_cast<const uint8_t*>(handle_.baseAddr)
                               + hdr.globalAccumOffset;
            std::memcpy(globalOut->data(), gsrc, globalBytes);
        }

        // 阶段 3：校验 seqNo 是否变化
        std::atomic_thread_fence(std::memory_order_acquire);
        uint64_t seq2 = hdr.dumpSeqNo.load(std::memory_order_acquire);
        if (seq1 == seq2) {
            return true;
        }

        // seqNo 在读取期间被改变，随机退避后 retry
        std::this_thread::sleep_for(std::chrono::microseconds(waitDist(gen)));
    }

    return false;
}

}
