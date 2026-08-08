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

#ifdef SPDIAG_ENABLE_OB_CACHE


#include "cache_sampler.h"
#include "spdiag/log_macros.h"

#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <syscall.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

namespace SpDiag {

CacheHitRateSampler::CacheHitRateSampler() = default;

CacheHitRateSampler::~CacheHitRateSampler() {
    Close();
}

// Encode a perf_event_open config for PERF_TYPE_HW_CACHE.
// Layout: [cache level (bits 0-7)] [operation (bits 8-15)] [result (bits 16-23)]
// We always use OP_READ; result is either ACCESS or MISS.
uint64_t CacheHitRateSampler::BuildHwCacheConfig(const std::string& level, bool miss) const {
    static const std::unordered_map<std::string, uint64_t> levelMap = {
        {"L1-dcache", PERF_COUNT_HW_CACHE_L1D},
        {"L1-icache", PERF_COUNT_HW_CACHE_L1I},
        {"LLC",  PERF_COUNT_HW_CACHE_LL},
        {"dTLB", PERF_COUNT_HW_CACHE_DTLB},
        {"iTLB", PERF_COUNT_HW_CACHE_ITLB},
        {"branch",  PERF_COUNT_HW_CACHE_BPU},
    };

    auto it = levelMap.find(level);
    if (it == levelMap.end()) {
        return UINT64_MAX;
    }

    uint64_t config = it->second;
    config |= static_cast<uint64_t>(PERF_COUNT_HW_CACHE_OP_READ) << 8;
    if (miss) {
        config |= static_cast<uint64_t>(PERF_COUNT_HW_CACHE_RESULT_MISS) << 16;
    } else {
        config |= static_cast<uint64_t>(PERF_COUNT_HW_CACHE_RESULT_ACCESS) << 16;
    }
    return config;
}

bool CacheHitRateSampler::IsTargetAlive() const {
    if (targetPid_ <= 0) return false;
    return ::kill(targetPid_, 0) == 0;
}

int CacheHitRateSampler::ProbePmuCounterCount() {
    // Method 1: sysfs (rarely available on standard kernels)
    {
        const char* paths[] = {
            "/sys/bus/event_source/devices/armv8_pmuv3/caps/counters",
        };
        for (auto path : paths) {
            std::ifstream f(path);
            int n = 0;
            if (f >> n && n > 0) return n;
        }
    }

    // Method 2: parse kernel boot log for "N counters available"
    {
        std::ifstream f("/dev/kmsg");
        if (f) {
            std::string line;
            while (std::getline(f, line)) {
                auto pos = line.find("counters available");
                if (pos == std::string::npos) continue;
                auto numEnd = pos;
                while (numEnd > 0 && !std::isdigit(static_cast<unsigned char>(line[numEnd - 1]))) --numEnd;
                auto numStart = numEnd;
                while (numStart > 0 && std::isdigit(static_cast<unsigned char>(line[numStart - 1]))) --numStart;
                if (numStart < numEnd) {
                    try {
                        int n = std::stoi(line.substr(numStart, numEnd - numStart));
                        if (n > 0 && n <= 32) return n;
                    } catch (const std::exception& e) {
                        SPDIAG_LOG_ERROR(LogModule::PLATFORM, "Parse CPU count failed: '%s', exception: %s", 
                                         line.c_str(), e.what());
                    } catch (...) {
                        SPDIAG_LOG_ERROR(LogModule::PLATFORM, "Parse CPU count unknown exception");
                    }
                }
            }
        }
    }

    return 0;
}

// Open all perf events for one thread.
// Key design: pid=tid, cpu=-1 means counters are bound to this thread's task_struct.
// The kernel context-switch path saves/restores PMU registers per thread, so:
//   - Thread on-CPU → counters increment
//   - Thread sleeping / preempted / I/O wait → counters stop
// This guarantees all metrics reflect only actual CPU time, not wall-clock time.
ThreadFdSet CacheHitRateSampler::OpenForThread(pid_t tid) {
    ThreadFdSet tfs;
    tfs.tid = tid;

    for (const auto& level : levels_) {
        CacheLevelFd cfd;
        cfd.levelName = level;

        // Open ref (access) event as group leader.
        // attr.disabled=1 prevents counting until we explicitly enable.
        // PERF_FORMAT_TOTAL_TIME_ENABLED|RUNNING is needed for multiplexing scaling.
        uint64_t refConfig = BuildHwCacheConfig(level, false);
        if (refConfig == UINT64_MAX) continue;

        struct perf_event_attr attr = {};
        attr.type = PERF_TYPE_HW_CACHE;
        attr.size = sizeof(attr);
        attr.config = refConfig;
        attr.disabled = 1;
        attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;

        int refFd = static_cast<int>(syscall(__NR_perf_event_open, &attr, tid, -1, -1, 0));
        if (refFd < 0) continue;
        cfd.refFd = refFd;

        // Open miss event as group member — group_fd=refFd ties it to the same group.
        // The kernel schedules the entire group atomically: either all members are
        // on the PMU together, or none are. This ensures ref and miss cover the
        // same time window, making the hit rate calculation coherent.
        uint64_t missConfig = BuildHwCacheConfig(level, true);
        if (missConfig == UINT64_MAX) {
            close(cfd.refFd);
            cfd.refFd = -1;
            continue;
        }

        attr.config = missConfig;
        int missFd = static_cast<int>(syscall(__NR_perf_event_open, &attr, tid, -1, refFd, 0));
        if (missFd < 0) {
            close(cfd.refFd);
            cfd.refFd = -1;
            continue;
        }
        cfd.missFd = missFd;

        // PERF_IOC_FLAG_GROUP operates on all members atomically.
        // Reset clears counters to zero; enable starts counting.
        // Must be done after both fds are open so the group is complete.
        ioctl(refFd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
        ioctl(refFd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);

        tfs.levels.push_back(std::move(cfd));
    }

    // --- Hardware events: CPU_CYCLES + INSTRUCTIONS ---
    // Separate group from cache events. If we merged all events into one group
    // (e.g. 3 levels × 2 + 2 = 8 members), the PMU might refuse to schedule it
    // at all when hardware only has 6 counters. Two smaller groups allow the
    // kernel's round-robin multiplexer to time-share them independently.
    struct perf_event_attr hwAttr = {};
    hwAttr.type = PERF_TYPE_HARDWARE;
    hwAttr.size = sizeof(hwAttr);
    hwAttr.config = PERF_COUNT_HW_CPU_CYCLES;
    hwAttr.disabled = 1;
    hwAttr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;

    tfs.hw.cyclesFd = static_cast<int>(syscall(__NR_perf_event_open, &hwAttr, tid, -1, -1, 0));
    if (tfs.hw.cyclesFd >= 0) {
        hwAttr.config = PERF_COUNT_HW_INSTRUCTIONS;
        tfs.hw.instructionsFd = static_cast<int>(syscall(__NR_perf_event_open, &hwAttr,
                                                          tid, -1, tfs.hw.cyclesFd, 0));
        if (tfs.hw.instructionsFd >= 0) {
            ioctl(tfs.hw.cyclesFd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
            ioctl(tfs.hw.cyclesFd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
        } else {
            close(tfs.hw.cyclesFd);
            tfs.hw.cyclesFd = -1;
        }
    }

    return tfs;
}

void CacheHitRateSampler::CloseThread(ThreadFdSet& tfs) {
    for (auto& entry : tfs.levels) {
        if (entry.refFd >= 0) {
            ioctl(entry.refFd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
            if (entry.missFd >= 0) {
                close(entry.missFd);
                entry.missFd = -1;
            }
            close(entry.refFd);
            entry.refFd = -1;
        }
    }
    tfs.levels.clear();

    if (tfs.hw.cyclesFd >= 0) {
        ioctl(tfs.hw.cyclesFd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
        if (tfs.hw.instructionsFd >= 0) {
            close(tfs.hw.instructionsFd);
            tfs.hw.instructionsFd = -1;
        }
        close(tfs.hw.cyclesFd);
        tfs.hw.cyclesFd = -1;
    }
}

// Scan /proc/<pid>/task/ to discover the current thread list.
// Threads can be created or destroyed at any time (e.g. thread pool resize),
// so we reconcile on every Collect() call:
//   - Threads that disappeared → close their perf fds, remove from list
//   - New threads → open perf fds, add to list
void CacheHitRateSampler::RefreshThreads() {
    std::string taskDir = "/proc/" + std::to_string(targetPid_) + "/task";
    DIR* dir = opendir(taskDir.c_str());
    if (!dir) {
        return;
    }

    std::unordered_set<pid_t> activeTids;
    struct dirent* ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
        char* endp = nullptr;
        long tid = strtol(ent->d_name, &endp, 10);
        if (endp == ent->d_name || *endp != '\0') continue;
        activeTids.insert(static_cast<pid_t>(tid));
    }
    closedir(dir);

    // Close threads that no longer exist
    std::vector<pid_t> toRemove;
    for (auto& tfs : threadFds_) {
        if (activeTids.find(tfs.tid) == activeTids.end()) {
            CloseThread(tfs);
            toRemove.push_back(tfs.tid);
        }
    }
    if (!toRemove.empty()) {
        threadFds_.erase(
            std::remove_if(threadFds_.begin(), threadFds_.end(),
                [&toRemove](const ThreadFdSet& tfs) {
                    return std::find(toRemove.begin(), toRemove.end(), tfs.tid) != toRemove.end();
                }),
            threadFds_.end());
    }

    // Open counters for new threads
    for (pid_t tid : activeTids) {
        bool found = false;
        for (const auto& tfs : threadFds_) {
            if (tfs.tid == tid) { found = true; break; }
        }
        if (!found) {
            ThreadFdSet tfs = OpenForThread(tid);
            if (!tfs.levels.empty()) {
                threadFds_.push_back(std::move(tfs));
            }
        }
    }
}

bool CacheHitRateSampler::Open(const std::vector<std::string>& levels, pid_t pid) {
    if (opened_) {
        return false;
    }

    targetPid_ = pid;
    levels_ = levels;
    aggLastRef_.assign(levels.size(), 0);
    aggLastMiss_.assign(levels.size(), 0);

    RefreshThreads();

    if (threadFds_.empty()) {
        std::cerr << "cachestat: no cache levels available for pid " << pid << std::endl;
        return false;
    }

    pmuCounterCount_ = ProbePmuCounterCount();
    int eventsPerThread = static_cast<int>(levels_.size()) * 2 + 2;
    if (pmuCounterCount_ > 0 && eventsPerThread > pmuCounterCount_) {
        multiplexed_ = true;
    }

    opened_ = true;
    return true;
}

bool CacheHitRateSampler::Collect(DataSink* sink) {
    if (!opened_ || !sink) {
        return false;
    }

    // Layout returned by read() on a perf fd with TIME_ENABLED|TIME_RUNNING.
    //   value        — raw counter value since last reset
    //   time_enabled — nanoseconds the event was scheduled (enabled)
    //   time_running — nanoseconds the event was actually on the PMU
    // When running < enabled, the kernel multiplexed this event with others
    // because there were more events than hardware counters. The scaling
    // formula below estimates the true count: value × enabled / running.
    struct read_format {
        uint64_t value;
        uint64_t time_enabled;
        uint64_t time_running;
    };

    RefreshThreads();

    // Per-level aggregated scaled values across all threads.
    // We sum first, then compute deltas — this avoids negative deltas when a
    // thread exits (its fd reads fail, contributing 0, but other threads still
    // accumulate). The delta is against the previous cycle's aggregate total.
    std::vector<double> aggScaledRef(levels_.size(), 0.0);
    std::vector<double> aggScaledMiss(levels_.size(), 0.0);

    bool anyCollected = false;

    double aggScaledCycles = 0.0;
    double aggScaledInstructions = 0.0;

    for (const auto& tfs : threadFds_) {
        for (size_t li = 0; li < tfs.levels.size(); ++li) {
            const auto& entry = tfs.levels[li];

            // Find the level index
            size_t levelIdx = levels_.size();
            for (size_t k = 0; k < levels_.size(); ++k) {
                if (levels_[k] == entry.levelName) { levelIdx = k; break; }
            }
            if (levelIdx == levels_.size()) continue;

            read_format refData = {};
            read_format missData = {};

            if (entry.refFd >= 0) {
                if (read(entry.refFd, &refData, sizeof(refData)) != sizeof(refData)) {
                    continue;
                }
            }
            if (entry.missFd >= 0) {
                if (read(entry.missFd, &missData, sizeof(missData)) != sizeof(missData)) {
                    continue;
                }
            }

            // Multiplexing scaling: value × enabled / running.
            // If the PMU had enough counters, running ≈ enabled and this is a no-op.
            // Under multiplexing, the kernel round-robins event groups on/off the PMU;
            // the scaling extrapolates what the count would have been if it ran 100%.
            if (refData.time_running > 0) {
                aggScaledRef[levelIdx] += static_cast<double>(refData.value) *
                    static_cast<double>(refData.time_enabled) /
                    static_cast<double>(refData.time_running);
                anyCollected = true;
            }
            if (missData.time_running > 0) {
                aggScaledMiss[levelIdx] += static_cast<double>(missData.value) *
                    static_cast<double>(missData.time_enabled) /
                    static_cast<double>(missData.time_running);
            }
        }

        // Aggregate hardware events
        if (tfs.hw.cyclesFd >= 0) {
            read_format data = {};
            if (read(tfs.hw.cyclesFd, &data, sizeof(data)) == sizeof(data)
                && data.time_running > 0) {
                aggScaledCycles += static_cast<double>(data.value) *
                    static_cast<double>(data.time_enabled) /
                    static_cast<double>(data.time_running);
            }
        }
        if (tfs.hw.instructionsFd >= 0) {
            read_format data = {};
            if (read(tfs.hw.instructionsFd, &data, sizeof(data)) == sizeof(data)
                && data.time_running > 0) {
                aggScaledInstructions += static_cast<double>(data.value) *
                    static_cast<double>(data.time_enabled) /
                    static_cast<double>(data.time_running);
            }
        }
    }

    // Delta = current aggregate minus last aggregate.
    // The cast to uint64_t is safe because scaled values are non-negative.
    // If process just exited, fds fail → aggregate stays same → delta = 0,
    // which correctly means "no new work done this interval".
    uint64_t scaledCycles = static_cast<uint64_t>(aggScaledCycles);
    uint64_t scaledInstructions = static_cast<uint64_t>(aggScaledInstructions);
    uint64_t deltaCycles = 0;
    uint64_t deltaInstructions = 0;
    if (scaledCycles > aggLastCycles_) deltaCycles = scaledCycles - aggLastCycles_;
    if (scaledInstructions > aggLastInstructions_) deltaInstructions = scaledInstructions - aggLastInstructions_;
    aggLastCycles_ = scaledCycles;
    aggLastInstructions_ = scaledInstructions;

    // Accumulate for session averages
    cumCycles_ += deltaCycles;
    cumInstructions_ += deltaInstructions;

    // Elapsed wall-clock time for frequency/GIPS
    struct timespec ts = {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t nowNs = static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
                     static_cast<uint64_t>(ts.tv_nsec);
    double elapsedSec = 0.0;
    if (lastCollectTimeNs_ > 0 && nowNs > lastCollectTimeNs_) {
        elapsedSec = static_cast<double>(nowNs - lastCollectTimeNs_) / 1e9;
    }
    if (firstCollectTimeNs_ == 0 && (deltaCycles > 0 || deltaInstructions > 0)) {
        firstCollectTimeNs_ = nowNs;
    }
    // Time-freezing mechanism: only advance lastCollectTimeNs_ when we have real
    // data. When the target exits, deltas become 0, so lastCollectTimeNs_ stops.
    // The avg metrics use (lastCollectTimeNs_ - firstCollectTimeNs_) as the
    // denominator, so they freeze at the final value instead of decaying
    // (which would happen if we used nowNs — the numerator would stay constant
    // while the denominator kept growing).
    if (deltaCycles > 0 || deltaInstructions > 0) {
        lastCollectTimeNs_ = nowNs;
    }

    // avgElapsedSec is bounded by lastCollectTimeNs_, not nowNs.
    // After process exit: lastCollectTimeNs_ frozen → avgElapsedSec frozen →
    // avgFreq and avgGIPS remain at their final values.
    double avgElapsedSec = 0.0;
    if (firstCollectTimeNs_ > 0 && lastCollectTimeNs_ > firstCollectTimeNs_) {
        avgElapsedSec = static_cast<double>(lastCollectTimeNs_ - firstCollectTimeNs_) / 1e9;
    }

    bool allZero = true;

    for (size_t li = 0; li < levels_.size(); ++li) {
        uint64_t scaledRef = static_cast<uint64_t>(aggScaledRef[li]);
        uint64_t scaledMiss = static_cast<uint64_t>(aggScaledMiss[li]);

        if (scaledRef > aggLastRef_[li]) {
            uint64_t deltaRef = scaledRef - aggLastRef_[li];
            if (deltaRef > 0) allZero = false;
            uint64_t deltaMiss = 0;
            if (scaledMiss > aggLastMiss_[li]) {
                deltaMiss = scaledMiss - aggLastMiss_[li];
            }
            aggLastRef_[li] = scaledRef;
            aggLastMiss_[li] = scaledMiss;

            double hitRate = 1.0;
            if (deltaRef > 0) {
                hitRate = 1.0 - (static_cast<double>(deltaMiss) / static_cast<double>(deltaRef));
                if (hitRate < 0.0) hitRate = 0.0;
                if (hitRate > 1.0) hitRate = 1.0;
            }

            // MPKI (Misses Per Kilo Instructions): how many cache misses per 1000
            // instructions retired. Higher MPKI = more cache-related stalls.
            // Load Density (references / instructions): average cache accesses per
            // instruction. Values > 1.0 suggest memory-intensive code (e.g. memcpy).
            // Both use the same deltaInstructions for consistency.
            double mpki = (deltaInstructions > 0)
                ? static_cast<double>(deltaMiss) * 1000.0 / static_cast<double>(deltaInstructions)
                : 0.0;
            double loadDensity = (deltaInstructions > 0)
                ? static_cast<double>(deltaRef) / static_cast<double>(deltaInstructions)
                : 0.0;

            std::string prefix = "cache." + levels_[li];
            std::vector<std::pair<std::string, std::string>> items = {
                {prefix + ".references", std::to_string(deltaRef)},
                {prefix + ".misses", std::to_string(deltaMiss)},
                {prefix + ".hit_rate", std::to_string(hitRate)},
                {prefix + ".mpki", std::to_string(mpki)},
                {prefix + ".load_density", std::to_string(loadDensity)},
            };
            sink->SubmitBatch(items);
        } else {
            aggLastRef_[li] = scaledRef;
            aggLastMiss_[li] = scaledMiss;
        }
    }

    // CPU performance metrics — delta (instantaneous, per collection interval)
    // IPC: instructions per cycle — pipeline efficiency. ARM A76 max ≈ 4.0.
    // freq_ghz: effective CPU frequency = cycles / elapsed time. Multi-thread
    //   aggregation means this can exceed single-core max frequency.
    // gips: giga-instructions per second = IPC × frequency.
    // These are only emitted when delta > 0 (target is alive and running).
    if (deltaCycles > 0 || deltaInstructions > 0) {
        double ipc = (deltaCycles > 0)
            ? static_cast<double>(deltaInstructions) / static_cast<double>(deltaCycles)
            : 0.0;
        double freqGhz = (elapsedSec > 0)
            ? static_cast<double>(deltaCycles) / elapsedSec / 1e9
            : 0.0;
        double gips = (elapsedSec > 0)
            ? static_cast<double>(deltaInstructions) / elapsedSec / 1e9
            : 0.0;

        sink->SubmitBatch({
            {"cpu.ipc", std::to_string(ipc)},
            {"cpu.freq_ghz", std::to_string(freqGhz)},
            {"cpu.gips", std::to_string(gips)},
        });
    }

    // CPU performance metrics — session average.
    // Always emitted once cum > 0, even after the target exits.
    // The display layer (cache_display) shows these in the "Avg" row,
    // while the Delta row above shows the instantaneous per-interval values.
    // cum_instructions is also emitted so the display can recompute
    // MPKI/loadDensity in cumulative mode (cumRef / cumInstructions).
    if (cumCycles_ > 0 || cumInstructions_ > 0) {
        double avgIpc = (cumCycles_ > 0)
            ? static_cast<double>(cumInstructions_) / static_cast<double>(cumCycles_)
            : 0.0;
        double avgFreqGhz = (avgElapsedSec > 0)
            ? static_cast<double>(cumCycles_) / avgElapsedSec / 1e9
            : 0.0;
        double avgGips = (avgElapsedSec > 0)
            ? static_cast<double>(cumInstructions_) / avgElapsedSec / 1e9
            : 0.0;

        sink->SubmitBatch({
            {"cpu.avg_ipc", std::to_string(avgIpc)},
            {"cpu.avg_freq_ghz", std::to_string(avgFreqGhz)},
            {"cpu.avg_gips", std::to_string(avgGips)},
            {"cpu.cum_instructions", std::to_string(cumInstructions_)},
        });
    }

    // Emit counter info every cycle for display
    {
        int eventsPerThread = static_cast<int>(levels_.size()) * 2 + 2;
        std::string counterInfo = std::to_string(eventsPerThread) + "/" +
            (pmuCounterCount_ > 0 ? std::to_string(pmuCounterCount_) : "?");
        sink->SubmitBatch({{"cache._counter_info", counterInfo}});
        if (multiplexed_) {
            sink->SubmitBatch({{"cache._multiplexed", "1"}});
        }
    }

    if (anyCollected && allZero && !threadFds_.empty()) {
        ++consecutiveZeroCount_;
        if (consecutiveZeroCount_ >= ZERO_WARNING_THRESHOLD) {
            if (!IsTargetAlive()) {
                if (!targetExitedPrinted_) {
                    targetExitedPrinted_ = true;
                    std::cerr << "cachestat: target process (pid " << targetPid_
                              << ") has exited." << std::endl;
                }
            } else if (!zeroWarningPrinted_) {
                zeroWarningPrinted_ = true;
                std::cerr << "cachestat: PMU counters have been zero for "
                          << ZERO_WARNING_THRESHOLD << " consecutive polls.\n"
                          << "  Possible causes:\n"
                          << "  - Running in a VM without PMU passthrough\n"
                          << "  - Process is idle or not generating cache events\n"
                          << "  - Hardware PMU not available (try different --cache-levels)\n";
            }
        }
    } else {
        consecutiveZeroCount_ = 0;
        zeroWarningPrinted_ = false;
    }

    return true;
}

void CacheHitRateSampler::Close() {
    for (auto& tfs : threadFds_) {
        CloseThread(tfs);
    }
    threadFds_.clear();
    levels_.clear();
    aggLastRef_.clear();
    aggLastMiss_.clear();
    aggLastCycles_ = 0;
    aggLastInstructions_ = 0;
    lastCollectTimeNs_ = 0;
    cumCycles_ = 0;
    cumInstructions_ = 0;
    firstCollectTimeNs_ = 0;
    opened_ = false;
}

}

#endif // SPDIAG_ENABLE_OB_CACHE
