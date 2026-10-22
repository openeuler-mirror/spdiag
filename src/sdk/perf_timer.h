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
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <vector>
#include "spdiag/perf_shard.h"
#include "spdiag/shm_layout.h"

namespace SpDiag {

class ShmWriter;
class PerfShardManager;

class PerfTimer {
public:
    PerfTimer(ShmWriter& writer, PerfShardManager& shardManager, uint32_t intervalMs,
              uint32_t baseOffset, uint32_t slotCount, uint32_t programIndex);
    ~PerfTimer();

    void Start();
    void Stop();
    void SetInterval(uint32_t intervalMs);
    void TriggerDumpNow();

private:
    void DumpLoop();
    bool DoOneDump();
    void Acknowledge(uint64_t requestGeneration);
    bool ClearRequested(ShmHeader* header);
    void MergeShard(void* dst, uint32_t dstMaxSlots,
                    const PerfStat* srcStats,
                    const uint64_t* snapshotTopN);
    // 全局 PerfPoint 累加区快照+合并（须在 dumpSeqNo 持锁期内调用，单 writer 协议不变）。
    void SnapshotAndMergeGlobal();

    ShmWriter&          writer_;
    PerfShardManager&   shardManager_;
    uint32_t            intervalMs_;
    uint32_t            baseOffset_;
    uint32_t            slotCount_;
    uint32_t            programIndex_;
    uint32_t            generation_ = 0;
    std::thread         thread_;
    std::atomic<bool>   running_{false};
    std::atomic<bool>   localDumpRequested_{false};
    std::condition_variable cv_;
    std::mutex          cvMutex_;
    std::unique_ptr<uint64_t[]> snapshotTopN_;
    std::unique_ptr<uint16_t[]> snapshotTopNCounts_;
    std::unique_ptr<PerfStat[]> snapshotStats_;
    size_t snapshotTopNSize_ = 0;
    size_t snapshotTopNCountSize_ = 0;
    uint32_t snapshotStatsSize_ = 0;
    uint64_t observedDumpRequest_ = 0;
};

}
