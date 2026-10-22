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

#include "shm_writer.h"
#include <cstring>

namespace SpDiag {

ShmWriter::~ShmWriter() {
    Destroy();
}

bool ShmWriter::Create(const std::string& shmName, size_t totalSize) {
    handle_ = Platform::ShmCreate(shmName, totalSize);
    if (handle_.baseAddr == nullptr) {
        return false;
    }
    shmName_ = shmName;
    totalSize_ = totalSize;
    owner_ = true;
    return true;
}

bool ShmWriter::Open(const std::string& shmName, size_t totalSize) {
    handle_ = Platform::ShmOpenReadWrite(shmName, totalSize);
    if (handle_.baseAddr == nullptr) {
        return false;
    }
    shmName_ = shmName;
    totalSize_ = totalSize;
    owner_ = false;
    return true;
}

void ShmWriter::Destroy() {
    if (handle_.baseAddr != nullptr) {
        Platform::ShmClose(handle_);
        if (owner_) {
            Platform::ShmDestroy(shmName_);
        }
        handle_.baseAddr = nullptr;
        handle_.mappedSize = 0;
        handle_.fd = -1;

    }
}

void ShmWriter::Release() {
    if (handle_.baseAddr != nullptr) {
        Platform::ShmClose(handle_);
        handle_.baseAddr = nullptr;
        handle_.mappedSize = 0;
        handle_.fd = -1;

    }
    shmName_.clear();
    owner_ = false;
}

ShmHeader* ShmWriter::Header() {
    return reinterpret_cast<ShmHeader*>(handle_.baseAddr);
}

void* ShmWriter::ShardPtr(uint32_t programIndex, uint32_t coreIndex) {
    (void)programIndex;
    auto* hdr = Header();
    size_t shardSize = ShmShardSize(hdr->maxSlots,
                                    hdr->perCoreTopNCapacity);
    size_t offset = hdr->shardsOffset + coreIndex * shardSize;
    return reinterpret_cast<uint8_t*>(handle_.baseAddr) + offset;
}

GlobalPerfStat* ShmWriter::GlobalAccumPtr() {
    auto* hdr = Header();
    if (hdr == nullptr || hdr->globalAccumCount == 0) {
        return nullptr;
    }
    return static_cast<GlobalPerfStat*>(static_cast<void*>(
        reinterpret_cast<uint8_t*>(handle_.baseAddr) +
        hdr->globalAccumOffset));
}

void ShmWriter::WriteHeader(const ShmHeader& hdr) {
    ShmHeader* dst = Header();
    dst->magic = hdr.magic;
    dst->version = hdr.version;
    dst->mode = hdr.mode;
    dst->levelGate = hdr.levelGate;
    std::memcpy(dst->reserved, hdr.reserved, sizeof(hdr.reserved));
    dst->maxSlots = hdr.maxSlots;
    dst->maxPrograms = hdr.maxPrograms;
    dst->registeredPrograms = hdr.registeredPrograms;
    dst->numCores = hdr.numCores;
    dst->perCoreTopNCapacity = hdr.perCoreTopNCapacity;
    dst->dumpIntervalMs = hdr.dumpIntervalMs;
    dst->generation = hdr.generation;
    if (dst != &hdr) {
        dst->dumpRequestGeneration.store(
            hdr.dumpRequestGeneration.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        dst->dumpSeqNo.store(hdr.dumpSeqNo.load(std::memory_order_relaxed),
                             std::memory_order_relaxed);
    }
    dst->lastDumpTimestampNs = hdr.lastDumpTimestampNs;
    dst->shmTotalSize = hdr.shmTotalSize;
    dst->registryOffset = hdr.registryOffset;
    dst->keyNamesOffset = hdr.keyNamesOffset;
    dst->keyModuleNamesOffset = hdr.keyModuleNamesOffset;
    dst->activeBitmapOffset = hdr.activeBitmapOffset;
    dst->shardsOffset = hdr.shardsOffset;
#ifdef SPDIAG_ENABLE_PERFLOG
    dst->perflogEnabled = hdr.perflogEnabled;
    dst->perflogBootCounter = hdr.perflogBootCounter;
    dst->perflogBootSystemNs = hdr.perflogBootSystemNs;
    dst->perflogTimerFreq = hdr.perflogTimerFreq;
#endif
    dst->globalAccumOffset = hdr.globalAccumOffset;
    dst->globalAccumCount  = hdr.globalAccumCount;
}

void ShmWriter::WriteShard(uint32_t programIndex, uint32_t coreIndex, const void* data, size_t size) {
    std::memcpy(ShardPtr(programIndex, coreIndex), data, size);
}

size_t ShmWriter::TotalSize() const {
    return totalSize_;
}

size_t ShmWriter::ObjectSize() const {
    return handle_.objectSize;
}

}
