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
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <memory>
#include <algorithm>

namespace SpDiag {

constexpr uint32_t shmMagic          = 0x50455246;
constexpr uint32_t maxProgramsDefault = 8;
constexpr uint32_t MAX_PERF_SLOTS    = 256;
constexpr uint32_t MAX_GLOBAL_POINTS = MAX_PERF_SLOTS;
#ifdef SPDIAG_ENABLE_PERCENTILE
constexpr uint32_t maxTopN           = 100;
#endif
constexpr uint32_t perflogTimestampCount = 100;
constexpr uint32_t nameLen           = 64;

struct ProgramEntry {
    char     name[64];
    uint32_t baseSlot;
    uint32_t slotCount;
    uint32_t numCores;
    uint32_t shardOffset;
    int32_t  pid;
    uint8_t  active;
    uint8_t  reserved[3];
};

struct alignas(64) PerfStat {
    uint32_t                  slotIndex;
    uint32_t                  backGoodCount;
    uint32_t                  backBadCount;
    uint32_t                  backNotCount;
    uint32_t                  tickCount;
    uint64_t                  totalTimeNs;
    uint64_t                  minTimeNs;
    uint64_t                  maxTimeNs;
#ifdef SPDIAG_ENABLE_PERCENTILE
    uint64_t                  topN[maxTopN];
    uint8_t                   topNCount;
    uint8_t                   level;
    uint8_t                   reserved[2];
#else
    uint8_t                   level;
    uint8_t                   reserved[3];
#endif
#ifdef SPDIAG_ENABLE_PERFLOG
    uint64_t                  timestamps[perflogTimestampCount];
    uint64_t                  perflogCosts[perflogTimestampCount];
    uint16_t                  perflogThreadIds[perflogTimestampCount];
    std::atomic<uint32_t>     perflogWritePos;
    uint8_t                   reserved2[2];
#endif

    void reset() {
        slotIndex = 0;
        backGoodCount = 0;
        backBadCount = 0;
        backNotCount = 0;
        tickCount = 0;
        totalTimeNs = 0;
        minTimeNs = 0;
        maxTimeNs = 0;
#ifdef SPDIAG_ENABLE_PERCENTILE
        std::fill_n(topN, maxTopN, 0ULL);
        topNCount = 0;
#endif
        level = 0;
        std::memset(reserved, 0, sizeof(reserved));
#ifdef SPDIAG_ENABLE_PERFLOG
        std::memset(timestamps, 0, sizeof(timestamps));
        std::memset(perflogCosts, 0, sizeof(perflogCosts));
        std::memset(perflogThreadIds, 0, sizeof(perflogThreadIds));
        perflogWritePos.store(0, std::memory_order_relaxed);
        std::memset(reserved2, 0, sizeof(reserved2));
#endif
    }

    void copy(const PerfStat& other) {
        slotIndex = other.slotIndex;
        backGoodCount = other.backGoodCount;
        backBadCount = other.backBadCount;
        backNotCount = other.backNotCount;
        tickCount = other.tickCount;
        totalTimeNs = other.totalTimeNs;
        minTimeNs = other.minTimeNs;
        maxTimeNs = other.maxTimeNs;
#ifdef SPDIAG_ENABLE_PERCENTILE
        std::copy_n(other.topN, maxTopN, topN);
        topNCount = other.topNCount;
#endif
        level = other.level;
        std::memcpy(reserved, other.reserved, sizeof(reserved));
#ifdef SPDIAG_ENABLE_PERFLOG
        std::memcpy(timestamps, other.timestamps, sizeof(timestamps));
        std::memcpy(perflogCosts, other.perflogCosts, sizeof(perflogCosts));
        std::memcpy(perflogThreadIds, other.perflogThreadIds, sizeof(perflogThreadIds));
        perflogWritePos.store(other.perflogWritePos.load(std::memory_order_relaxed), std::memory_order_relaxed);
        std::memcpy(reserved2, other.reserved2, sizeof(reserved2));
#endif
    }
};

struct LocalPerfShard {
    uint32_t coreId;
    PerfStat stats[MAX_PERF_SLOTS];

    void reset() {
        coreId = 0;
        for (uint32_t i = 0; i < MAX_PERF_SLOTS; ++i) {
            stats[i].reset();
        }
    }

    void copy(const LocalPerfShard& other) {
        coreId = other.coreId;
        for (uint32_t i = 0; i < MAX_PERF_SLOTS; ++i) {
            stats[i].copy(other.stats[i]);
        }
    }
};

// 全局 PerfPoint 的进程内原子累加器（每键独占一条 cache line，避免 false-sharing）。
// counts/totalTime 用 relaxed fetch_add 累加；min/max 用 CAS；topN/百分位对全局舍弃。
// minTimeNs==0 表示尚未采样（CAS 时视作 +∞）。
struct alignas(64) GlobalAccum {
    std::atomic<uint64_t> goodCount{0};
    std::atomic<uint64_t> badCount{0};
    std::atomic<uint64_t> notCount{0};
    std::atomic<uint64_t> tickCount{0};
    std::atomic<uint64_t> totalTimeNs{0};
    std::atomic<uint64_t> minTimeNs{0};
    std::atomic<uint64_t> maxTimeNs{0};
    std::atomic<uint32_t> level{0};
    char _pad[4];
#ifdef SPDIAG_ENABLE_PERCENTILE
    // 无锁 top-100 最大耗时（best-effort 近似，用于 P99 计算）
    // 多线程并发写入时可能有少量丢失，对百分位精度影响可忽略
    uint64_t               topN[maxTopN]{};
    std::atomic<uint32_t>  topNCount{0};
    char                   _pad2[4];
#endif
};
static_assert(sizeof(GlobalAccum) % 64 == 0,
              "GlobalAccum must be cache-line aligned (multiple of 64); layout corruption detected");

#ifdef SPDIAG_ENABLE_PERCENTILE

inline void SiftDownMin(uint64_t* heap, uint8_t size) {
    uint32_t i = 0;
    while (true) {
        uint32_t left = 2 * i + 1;
        uint32_t right = 2 * i + 2;
        uint32_t smallest = i;
        if (left < size && heap[left] < heap[smallest]) {
            smallest = left;
        }
        if (right < size && heap[right] < heap[smallest]) {
            smallest = right;
        }
        if (smallest == i) {
            break;
        }
        std::swap(heap[i], heap[smallest]);
        i = smallest;
    }
}

inline void InsertTopN(PerfStat& stat, uint64_t elapsed) {
    if (stat.topNCount < maxTopN) {
        stat.topN[stat.topNCount] = elapsed;
        ++stat.topNCount;
        std::push_heap(stat.topN, stat.topN + stat.topNCount, std::greater<uint64_t>());
    } else if (elapsed > stat.topN[0]) {
        stat.topN[0] = elapsed;
        SiftDownMin(stat.topN, stat.topNCount);
    }
}

// 全局 PerfPoint 专用的无锁 topN 更新（best-effort 近似）。
// 多线程并发写入可能有少量样本丢失，对 P99 精度影响可忽略。
inline void UpdateTopNAtomic(GlobalAccum& a, uint64_t v) {
    uint32_t n = a.topNCount.load(std::memory_order_relaxed);
    if (n < maxTopN) {
        a.topN[n] = v;
        a.topNCount.store(n + 1, std::memory_order_release);
        return;
    }
    // 数组已满，扫描找最小值并替换
    uint64_t minVal = a.topN[0];
    uint32_t minIdx = 0;
    for (uint32_t i = 1; i < maxTopN; ++i) {
        if (a.topN[i] < minVal) {
            minVal = a.topN[i];
            minIdx = i;
        }
    }
    if (v > minVal) {
        a.topN[minIdx] = v;
    }
}
#endif

inline size_t ShmShardSize(uint32_t maxSlots) {
    return sizeof(uint32_t) + maxSlots * sizeof(PerfStat);
}

inline PerfStat* ShmShardStats(void* shardPtr) {
    return reinterpret_cast<PerfStat*>(static_cast<uint8_t*>(shardPtr) + sizeof(uint32_t));
}

inline const PerfStat* ShmShardStats(const void* shardPtr) {
    return reinterpret_cast<const PerfStat*>(static_cast<const uint8_t*>(shardPtr) + sizeof(uint32_t));
}

struct ShmHeader {
    uint32_t                magic;
    uint32_t                version = 3;
    uint8_t                 mode;
    uint8_t                 levelGate;
    uint8_t                 reserved[2];
    uint32_t                maxSlots;
    uint32_t                maxPrograms;
    uint32_t                registeredPrograms;
    uint32_t                numCores;
    uint32_t                dumpIntervalMs;
    uint32_t                generation;     // 每次 CreateShm 递增，供 SDK 检测 SHM 重启
    // dumpSeqNo 偶数/奇数锁协议：偶数=空闲，奇数=持锁中。
    // 约束：整个系统有且仅有一个 writer 线程（PerfTimer）执行 CAS +1/-1。
    // 增加第二个 writer 会破坏协议，需重新设计锁层级。
    std::atomic<uint64_t>   dumpSeqNo;
    uint64_t                lastDumpTimestampNs;
    size_t                  shmTotalSize;
    size_t                  registryOffset;
    size_t                  keyNamesOffset;
    size_t                  keyModuleNamesOffset;
    size_t                  activeBitmapOffset;
    size_t                  shardsOffset;
    uint8_t                 reserved4[3];
    std::atomic_flag        spinlock;
#ifdef SPDIAG_ENABLE_PERFLOG
    uint8_t                 perflogEnabled;
    uint64_t                perflogBootCounter;
    uint64_t                perflogBootSystemNs;
    uint64_t                perflogTimerFreq;
#endif
    size_t                  globalAccumOffset = 0;
    uint32_t                globalAccumCount  = 0;
};

static_assert(std::atomic<uint64_t>::is_always_lock_free,
              "dumpSeqNo must be lock-free for shared memory usage");
static_assert(sizeof(std::atomic<uint64_t>) == sizeof(uint64_t),
              "atomic<uint64_t> size mismatch");
static_assert(alignof(std::atomic<uint64_t>) == alignof(uint64_t),
              "atomic<uint64_t> alignment mismatch");

class ShardBuffer {
public:
    ShardBuffer() = default;

    void resize(size_t count, uint32_t maxSlots) {
        shardSize_ = ShmShardSize(maxSlots);
        maxSlots_ = maxSlots;
        data_ = std::make_unique<uint8_t[]>(count * shardSize_);
        count_ = count;
        std::memset(data_.get(), 0, count * shardSize_);
    }

    PerfStat* stats(size_t shardIdx) {
        return ShmShardStats(data_.get() + shardIdx * shardSize_);
    }

    const PerfStat* stats(size_t shardIdx) const {
        return ShmShardStats(data_.get() + shardIdx * shardSize_);
    }

    void* shardData(size_t shardIdx) {
        return data_.get() + shardIdx * shardSize_;
    }

    const void* shardData(size_t shardIdx) const {
        return data_.get() + shardIdx * shardSize_;
    }

    uint32_t maxSlots() const { return maxSlots_; }
    size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }

private:
    std::unique_ptr<uint8_t[]> data_;
    size_t count_ = 0;
    size_t shardSize_ = 0;
    uint32_t maxSlots_ = 0;
};

}
