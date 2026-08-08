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

#include "spdiag/shm_layout.h"
#include "platform.h"

#include <iostream>
#include <chrono>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <random>
#include <map>
#include <iomanip>
#include <thread>

using namespace SpDiag;

int main(int argc, char* argv[]) {
    std::string shmName = "/spdiag_shm_default";
    int durationSec = 30;
    int intervalMs = 500;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            shmName = argv[++i];
        } else if (std::strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            durationSec = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            intervalMs = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            std::cout << "Usage: test_dump_conflict_reader [options]\n"
                      << "  -s <shmName>    Shared memory name (default /spdiag_shm_default)\n"
                      << "  -d <seconds>    Duration (default 30)\n"
                      << "  -i <ms>         Read interval (default 500)\n";
            return 0;
        }
    }

    // 先只读 header 大小，探测共享内存存在
    ShmHandle probe = Platform::ShmOpenReadOnly(shmName, sizeof(ShmHeader));
    if (probe.baseAddr == nullptr) {
        std::cerr << "Failed to open shared memory: " << shmName << "\n";
        return 1;
    }

    const ShmHeader* probeHdr = reinterpret_cast<const ShmHeader*>(probe.baseAddr);
    if (probeHdr->magic != shmMagic) {
        std::cerr << "Invalid shared memory magic\n";
        Platform::ShmClose(probe);
        return 1;
    }

    size_t totalSize = probeHdr->shmTotalSize;
    Platform::ShmClose(probe);

    // 重新打开完整大小
    ShmHandle handle = Platform::ShmOpenReadOnly(shmName, totalSize);
    if (handle.baseAddr == nullptr) {
        std::cerr << "Failed to open full shared memory\n";
        return 1;
    }

    const ShmHeader* hdr = reinterpret_cast<const ShmHeader*>(handle.baseAddr);

    std::cout << "=== Conflict Reader ===\n"
              << "  SHM: " << shmName << "\n"
              << "  Size: " << totalSize << " bytes\n"
              << "  Programs: " << hdr->registeredPrograms << "\n"
              << "  Cores: " << hdr->numCores << "\n"
              << "  MaxSlots: " << hdr->maxSlots << "\n"
              << "  Duration: " << durationSec << "s\n"
              << "  Interval: " << intervalMs << "ms\n\n" << std::flush;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> waitDist(0, 50);

    int totalReads = 0;
    int successReads = 0;
    int failedReads = 0;
    int maxRetries = 0;
    std::map<int, int> retryDistribution;

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(durationSec);
    int readIndex = 0;

    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));

        ++readIndex;
        ++totalReads;

        // 复制 shm_reader.cpp 的逻辑，但统计 retry
        int retriesUsed = 0;
        bool success = false;
        ShardBuffer buffer;

        for (int retry = 0; retry < 20; ++retry) {
            // 阶段 1：等待 seqNo 变偶
            uint64_t seq1 = 0;
            bool gotEven = false;
            for (int wait = 0; wait < 200; ++wait) {
                seq1 = hdr->dumpSeqNo.load(std::memory_order_acquire);
                if (seq1 % 2 == 0) { gotEven = true; break; }
                std::this_thread::sleep_for(std::chrono::microseconds(waitDist(gen)));
            }
            if (!gotEven) continue;

            // 阶段 2：memcpy
            std::atomic_thread_fence(std::memory_order_acquire);
            uint32_t numShards = hdr->numCores;
            buffer.resize(numShards, hdr->maxSlots);

            size_t shardSize = ShmShardSize(hdr->maxSlots);
            for (uint32_t i = 0; i < numShards; ++i) {
                const void* src = reinterpret_cast<const uint8_t*>(handle.baseAddr)
                    + hdr->shardsOffset + i * shardSize;
                std::memcpy(buffer.shardData(i), src, shardSize);
            }

            // 阶段 3：校验
            std::atomic_thread_fence(std::memory_order_acquire);
            uint64_t seq2 = hdr->dumpSeqNo.load(std::memory_order_acquire);
            if (seq1 == seq2) {
                success = true;
                retriesUsed = retry;
                break;
            }

            // 失败，随机退避
            std::this_thread::sleep_for(std::chrono::microseconds(waitDist(gen)));
            retriesUsed = retry + 1;  // 记录到下一次 retry 前
        }

        if (success) {
            ++successReads;
            ++retryDistribution[retriesUsed];
            if (retriesUsed > maxRetries) maxRetries = retriesUsed;
            std::cout << "[Read #" << readIndex << "] success, " << retriesUsed << " retries\n" << std::flush;
        } else {
            ++failedReads;
            ++retryDistribution[20];
            maxRetries = 20;
            std::cout << "[Read #" << readIndex << "] FAILED after 20 retries\n" << std::flush;
        }
    }

    Platform::ShmClose(handle);

    std::cout << "\n=== Summary ===\n"
              << "  Total reads: " << totalReads << "\n"
              << "  Success: " << successReads << "\n"
              << "  Failed: " << failedReads << "\n"
              << "  Retry distribution:\n";
    for (const auto& [r, count] : retryDistribution) {
        std::cout << "    " << r << " retries: " << count << " times\n";
    }
    std::cout << "  Max retries: " << maxRetries << "\n";

    return failedReads > 0 ? 1 : 0;
}
