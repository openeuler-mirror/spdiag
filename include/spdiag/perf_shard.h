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
#include <cstdint>
#include <memory>
#include <vector>
#include "spdiag/shm_layout.h"

namespace SpDiag {

#ifdef SPDIAG_ENABLE_PERCENTILE
struct PerCoreTopNView {
    std::atomic<uint64_t>* values = nullptr;
    std::atomic<uint16_t>* count = nullptr;
    std::atomic<uint32_t>* sequence = nullptr;
    uint16_t* initialized = nullptr;
    uint32_t capacity = 0;
};
#endif

class PerfShardManager {
public:
    // Kept for source and binary compatibility with the v1 SDK.
    void Init(uint32_t numCores);
    bool Init(uint32_t numCores, uint32_t slotCount,
              uint32_t perCoreTopNCapacity);
    LocalPerfShard& GetShard();
    LocalPerfShard* GetShardArray();
    uint32_t GetNumCores() const { return numCores_; }
    uint32_t GetSlotCount() const { return slotCount_; }
    uint32_t GetPerCoreTopNCapacity() const {
        return perCoreTopNCapacity_;
    }
    void SetClearGeneration(uint64_t generation) {
        __atomic_store_n(&clearGeneration_, generation, __ATOMIC_RELEASE);
    }
    uint64_t GetClearGeneration() const {
        return __atomic_load_n(&clearGeneration_, __ATOMIC_ACQUIRE);
    }
    __attribute__((always_inline)) inline void PrepareForSample(
        LocalPerfShard& shard) {
        const uint64_t generation =
            __atomic_load_n(&clearGeneration_, __ATOMIC_ACQUIRE);
        if (__builtin_expect(
                shard.clearGeneration.load(std::memory_order_relaxed) ==
                    generation,
                1)) {
            return;
        }
        for (uint32_t slot = 0; slot < slotCount_; ++slot) {
#ifdef SPDIAG_ENABLE_PERCENTILE
            auto view = GetTopN(shard, slot);
            const uint32_t sequence = BeginTopNWrite(view);
            view.count->store(0, std::memory_order_relaxed);
            EndTopNWrite(view, sequence);
#endif
            shard.stats[slot].reset();
        }
        shard.clearGeneration.store(generation, std::memory_order_release);
    }
#ifdef SPDIAG_ENABLE_PERCENTILE
    __attribute__((always_inline)) inline PerCoreTopNView GetTopN(
        LocalPerfShard& shard, uint32_t slot) {
        if (slot >= slotCount_ || shard.perCoreTopNValues[slot] == nullptr ||
            shard.perCoreTopNCounts[slot] == nullptr ||
            shard.perCoreTopNSequences[slot] == nullptr) {
            return {};
        }
        return {shard.perCoreTopNValues[slot],
                shard.perCoreTopNCounts[slot],
                shard.perCoreTopNSequences[slot],
                &shard.perCoreTopNInitialized[slot], perCoreTopNCapacity_};
    }

    __attribute__((always_inline)) inline void InsertTopNValue(
        LocalPerfShard& shard, uint32_t slot, uint64_t elapsed) {
        InsertTopNUnchecked(
            GetTopN(shard, slot),
            elapsed);
    }

    void RestoreTopN(LocalPerfShard& shard, uint32_t slot,
                     ConstTopNView source);
    bool SnapshotTopN(uint64_t* values, uint16_t* counts,
                      LocalPerfShard& active);
#endif
    void SnapshotStatsAndReset(PerfStat* snapshotStats,
                               LocalPerfShard& active);
    void SnapshotAndReset(LocalPerfShard& snapshot, LocalPerfShard& active);

private:
#ifdef SPDIAG_ENABLE_PERCENTILE
    static_assert(std::atomic<uint64_t>::is_always_lock_free,
                  "per-core Top-N values require lock-free atomics");
    static_assert(std::atomic<uint16_t>::is_always_lock_free,
                  "per-core Top-N count requires lock-free atomics");
    struct PerCoreTopNStorage {
        std::unique_ptr<std::atomic<uint64_t>[]> values;
        std::unique_ptr<std::atomic<uint16_t>[]> counts;
        std::unique_ptr<std::atomic<uint32_t>[]> sequences;
    };

    __attribute__((always_inline)) static inline uint32_t BeginTopNWrite(
        PerCoreTopNView view) {
        const uint32_t sequence = view.sequence->load(std::memory_order_relaxed);
        view.sequence->store(sequence + 1, std::memory_order_relaxed);
        // Publish the odd marker before any heap cell is modified.  A release
        // store alone does not order the stores that follow it on weakly
        // ordered CPUs such as ARM64.
        std::atomic_thread_fence(std::memory_order_acq_rel);
        return sequence;
    }

    __attribute__((always_inline)) static inline void EndTopNWrite(
        PerCoreTopNView view, uint32_t sequence) {
        // Publish the even marker only after every heap cell is visible.
        std::atomic_thread_fence(std::memory_order_release);
        view.sequence->store(sequence + 2, std::memory_order_relaxed);
    }

    __attribute__((always_inline)) static inline void InsertTopNUnchecked(
        PerCoreTopNView view, uint64_t elapsed) {
        uint32_t count = std::min<uint32_t>(
            view.count->load(std::memory_order_relaxed), view.capacity);
        if (count == view.capacity &&
            elapsed <= view.values[0].load(std::memory_order_relaxed)) {
            return;
        }
        const uint32_t sequence = BeginTopNWrite(view);
        count = std::min<uint32_t>(
            view.count->load(std::memory_order_relaxed), view.capacity);
        if (count < view.capacity) {
            if (count >= *view.initialized) {
                std::atomic_init(view.values + count, elapsed);
                *view.initialized = static_cast<uint16_t>(count + 1);
            } else {
                view.values[count].store(elapsed, std::memory_order_relaxed);
            }
            uint32_t current = count;
            while (current > 0) {
                const uint32_t parent = (current - 1) / 2;
                const uint64_t parentValue =
                    view.values[parent].load(std::memory_order_relaxed);
                if (parentValue <= elapsed) {
                    break;
                }
                view.values[current].store(parentValue,
                                           std::memory_order_relaxed);
                current = parent;
            }
            view.values[current].store(elapsed, std::memory_order_relaxed);
            view.count->store(static_cast<uint16_t>(count + 1),
                              std::memory_order_release);
            EndTopNWrite(view, sequence);
            return;
        }
        if (elapsed <= view.values[0].load(std::memory_order_relaxed)) {
            EndTopNWrite(view, sequence);
            return;
        }
        uint32_t current = 0;
        while (true) {
            const uint32_t left = current * 2 + 1;
            if (left >= count) {
                break;
            }
            const uint32_t right = left + 1;
            uint32_t child = left;
            uint64_t childValue =
                view.values[left].load(std::memory_order_relaxed);
            if (right < count) {
                const uint64_t rightValue =
                    view.values[right].load(std::memory_order_relaxed);
                if (rightValue < childValue) {
                    child = right;
                    childValue = rightValue;
                }
            }
            if (childValue >= elapsed) {
                break;
            }
            view.values[current].store(childValue,
                                       std::memory_order_relaxed);
            current = child;
        }
        view.values[current].store(elapsed, std::memory_order_relaxed);
        EndTopNWrite(view, sequence);
    }
#endif

    std::unique_ptr<LocalPerfShard[]> shards_;
#ifdef SPDIAG_ENABLE_PERCENTILE
    PerCoreTopNStorage perCoreTopNStorage_;
#endif
    uint32_t numCores_ = 0;
    uint32_t slotCount_ = 0;
    uint32_t perCoreTopNCapacity_ = defaultPerCoreTopN;
    uint64_t clearGeneration_ = 0;
};

}
