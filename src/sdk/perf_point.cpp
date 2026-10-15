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

#include "spdiag/perf_point.h"
#include "spdiag/perf_manager.h"
#include "spdiag/shm_layout.h"
#include "global_perf_point_factory.h"
#include <algorithm>
#include <functional>
#include <cstdio>
#include <unistd.h>
#include <sys/syscall.h>

namespace SpDiag {

namespace {

constexpr auto RLX = std::memory_order_relaxed;

#if defined(__GNUC__) || defined(__clang__)
    #define LIKELY(x)   __builtin_expect(!!(x), 1)
    #define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define LIKELY(x)   (x)
    #define UNLIKELY(x) (x)
#endif

static uint64_t g_timerFreq = 0;

static inline uint64_t NowNs()
{
    uint64_t val;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
}
static inline uint64_t GetFreq()
{
    uint64_t val;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(val));
    return val;
}

static inline uint32_t GetThreadId() {
    static thread_local uint32_t cachedTid = 0;
    if (UNLIKELY(cachedTid == 0)) {
        cachedTid = static_cast<uint32_t>(syscall(SYS_gettid));
    }
    return cachedTid;
}

// 全局累加器的并发 min/max 更新（CAS loop）。min 的 0 视作「未采样/+∞」。
static inline void UpdateAtomicMin(std::atomic<uint64_t>& slot, uint64_t v) {
    uint64_t cur = slot.load(RLX);
    while (true) {
        if (cur != 0 && v >= cur) {
            break;   // 已有更小或相等的 min
        }
        if (slot.compare_exchange_weak(cur, v, RLX, std::memory_order_relaxed)) {
            break;
        }
    }
}

static inline void UpdateAtomicMax(std::atomic<uint64_t>& slot, uint64_t v) {
    uint64_t cur = slot.load(RLX);
    while (v > cur) {
        if (slot.compare_exchange_weak(cur, v, RLX, std::memory_order_relaxed)) {
            break;
        }
    }
}

}

PerfPoint::PerfPoint(uint32_t slotIndex, PerfLevel level)
    : slotIndex_(slotIndex), level_(level) {
    PerfManager::Instance().EnsureInitialized();
    if (g_timerFreq == 0) {
        g_timerFreq = GetFreq();
    }
    nsPerTick_ = (1000000000ULL << 32) / g_timerFreq;
    active_ = PerfManager::Instance().IsInitialized() &&
              PerfManager::Instance().IsLevelEnabled(level);
}

PerfPoint::PerfPoint(uint32_t slotIndex, global_perf_t, PerfLevel level)
    : slotIndex_(slotIndex), level_(level), global_(true) {
    PerfManager::Instance().EnsureInitialized();
    if (g_timerFreq == 0) {
        g_timerFreq = GetFreq();
    }
    nsPerTick_ = (1000000000ULL << 32) / g_timerFreq;
    PerfManager& mgr = PerfManager::Instance();
    if (!(mgr.IsInitialized() && mgr.IsLevelEnabled(level))) {
        active_ = false;
        return;
    }
    // 向工厂类取该 key 的共享原子累加器（flyweight）；越界/未初始化返回 nullptr → 降级 no-op。
    uint32_t gslot = mgr.FastResolveSlot(slotIndex);
    gaccum_ = GlobalPerfPointFactory::Instance().Acquire(gslot, level);
    active_ = (gaccum_ != nullptr);
}

PerfPoint::~PerfPoint() {
    if (active_ && !ended_) {
        Abandon();
    }
}

void PerfPoint::Start() {
    if (UNLIKELY(!active_)) {
        return;
    }
    if (global_) {
        if (UNLIKELY(!ended_)) {
            Abandon();
        }
        ended_ = false;
        // release：保证 startTsGlobal_ 的写入对随后 acquire 的 End 线程可见（跨线程 handoff）。
        startTsGlobal_.store(NowNs(), std::memory_order_release);
        return;
    }
    if(UNLIKELY(!ended_)) {
        Abandon();
    }
    ended_ = false;
    startTs_ = NowNs();
}

void PerfPoint::End(int returnCode) {
    if (UNLIKELY(!active_)) {
        return;
    }
    if (global_) {
        if (gaccum_ == nullptr) {
            return;
        }
        uint64_t endTs = NowNs();
        // acquire：与 Start 的 release 配对，必读到 Start 线程写的时间戳。
        uint64_t elapsed = endTs - startTsGlobal_.load(std::memory_order_acquire);
        // Widen before multiplying: the Q32 intermediate exceeds 64 bits near 4.295s.
        elapsed = static_cast<uint64_t>((static_cast<__uint128_t>(elapsed) * nsPerTick_) >> 32);
        if (LIKELY(returnCode == 0)) {
            gaccum_->goodCount.fetch_add(1, RLX);
        } else {
            gaccum_->badCount.fetch_add(1, RLX);
        }
        gaccum_->totalTimeNs.fetch_add(elapsed, RLX);
        UpdateAtomicMin(gaccum_->minTimeNs, elapsed);
        UpdateAtomicMax(gaccum_->maxTimeNs, elapsed);
        gaccum_->tickCount.fetch_add(1, RLX);
#ifdef SPDIAG_ENABLE_PERCENTILE
        UpdateTopNAtomic(*gaccum_, elapsed);
#endif
        ended_ = true;
        return;
    }
    uint64_t endTs = NowNs();
    uint64_t elapsed = endTs - startTs_;
    PerfManager& mgr = PerfManager::Instance();
    stat_ = &mgr.GetShard().stats[slotIndex_];
    stat_->slotIndex = mgr.FastResolveSlot(slotIndex_);
    stat_->level = static_cast<uint8_t>(level_);

    // Widen before multiplying, as in the global path and Platform::TicksToNs.
    elapsed = static_cast<uint64_t>((static_cast<__uint128_t>(elapsed) * nsPerTick_) >> 32);

    if (LIKELY(returnCode == 0)) {
        stat_->backGoodCount++;
    } else {
        stat_->backBadCount++;
    }

    stat_->totalTimeNs += elapsed;
    if (UNLIKELY(elapsed < stat_->minTimeNs || stat_->minTimeNs == 0)) {
        stat_->minTimeNs = elapsed;
    }
    if (UNLIKELY(elapsed > stat_->maxTimeNs)) {
        stat_->maxTimeNs = elapsed;
    }

    stat_->tickCount++;

#ifdef SPDIAG_ENABLE_PERCENTILE
    InsertTopN(*stat_, elapsed);
#endif
#ifdef SPDIAG_ENABLE_PERFLOG
    if (mgr.IsPerfLogEnabled()) {
        uint32_t idx = stat_->perflogWritePos.fetch_add(1, RLX) % perflogTimestampCount;
        stat_->timestamps[idx] = endTs;
        stat_->perflogCosts[idx] = elapsed;
        stat_->perflogThreadIds[idx] = static_cast<uint16_t>(GetThreadId());
    }
#endif
    ended_ = true;
}

void PerfPoint::Abandon() {
    if (UNLIKELY(!active_)) {
        return;
    }
    if (global_) {
        if (gaccum_ != nullptr) {
            gaccum_->notCount.fetch_add(1, RLX);
            gaccum_->tickCount.fetch_add(1, RLX);
        }
        return;
    }
    PerfManager& mgr = PerfManager::Instance();
    stat_ = &mgr.GetShard().stats[slotIndex_];
    stat_->slotIndex = mgr.FastResolveSlot(slotIndex_);
    stat_->level = static_cast<uint8_t>(level_);

    stat_->backNotCount++;
    stat_->tickCount++;
}

}
