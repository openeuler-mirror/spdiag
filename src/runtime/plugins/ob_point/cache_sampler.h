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

#ifdef SPDIAG_ENABLE_OB_CACHE


#include <cstdint>
#include <string>
#include <vector>
#include <sys/types.h>

#include "data_sink.h"

namespace SpDiag {

// Per-cache-level perf event pair: reference (access) and miss.
// refFd is the group leader, missFd is the group member — they are scheduled
// and reset atomically by the kernel as a single unit.
struct CacheLevelFd {
    std::string levelName;
    int refFd = -1;
    int missFd = -1;
};

// CPU hardware event pair: cycles (leader) + instructions (member).
// Used to derive IPC, effective frequency, and GIPS.
// Kept in a separate group from cache events to avoid oversized groups
// that the PMU cannot schedule at all.
struct HwEventFd {
    int cyclesFd = -1;
    int instructionsFd = -1;
};

// All perf event file descriptors for a single thread.
// Each thread gets its own set because perf_event_open(pid=tid, cpu=-1) binds
// counters to the thread's task_struct — the kernel saves/restores PMU registers
// on context switch, so counters only increment while this thread is on-CPU.
struct ThreadFdSet {
    pid_t tid = -1;
    std::vector<CacheLevelFd> levels;
    HwEventFd hw;
};

class CacheHitRateSampler {
public:
    CacheHitRateSampler();
    ~CacheHitRateSampler();

    CacheHitRateSampler(const CacheHitRateSampler&) = delete;
    CacheHitRateSampler& operator=(const CacheHitRateSampler&) = delete;

    bool Open(const std::vector<std::string>& levels, pid_t pid);
    bool Collect(DataSink* sink);
    void Close();

private:
    uint64_t BuildHwCacheConfig(const std::string& level, bool miss) const;
    static int ProbePmuCounterCount();
    bool IsTargetAlive() const;
    ThreadFdSet OpenForThread(pid_t tid);
    void CloseThread(ThreadFdSet& tfs);
    void RefreshThreads();

    std::vector<std::string> levels_;
    std::vector<ThreadFdSet> threadFds_;

    // Per-level aggregated last values — subtracted from current reading to get deltas.
    // Aggregated across all threads before differencing, so one thread exiting
    // doesn't cause a negative delta.
    std::vector<uint64_t> aggLastRef_;
    std::vector<uint64_t> aggLastMiss_;

    // Hardware event delta baselines (same pattern as cache deltas above)
    uint64_t aggLastCycles_ = 0;
    uint64_t aggLastInstructions_ = 0;

    // Wall-clock timestamp for computing instantaneous frequency/GIPS.
    // Only advanced when real data exists — freezes when target exits.
    uint64_t lastCollectTimeNs_ = 0;

    // Cumulative totals for session-average computation.
    // avg_ipc = cumInstructions / cumCycles, avg_freq = cumCycles / avgElapsedSec.
    // These persist after the target exits, keeping the final average stable.
    uint64_t cumCycles_ = 0;
    uint64_t cumInstructions_ = 0;
    uint64_t firstCollectTimeNs_ = 0;

    bool opened_ = false;
    pid_t targetPid_ = -1;
    int consecutiveZeroCount_ = 0;
    bool zeroWarningPrinted_ = false;
    bool targetExitedPrinted_ = false;
    int pmuCounterCount_ = 0;
    bool multiplexed_ = false;
    static constexpr int ZERO_WARNING_THRESHOLD = 10;
};

}

#endif // SPDIAG_ENABLE_OB_CACHE
