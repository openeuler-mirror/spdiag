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

#define SPDIAG_PROGRAM_NAME "user_def_test"
#include "spdiag/auto_perf.h"
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <cstdlib>

using namespace SpDiag;

static int passed = 0;
static int failed = 0;

#define CHECK(cond, msg) do { \
    if (cond) { std::cout << "  [PASS] " << msg << "\n"; ++passed; } \
    else      { std::cout << "  [FAIL] " << msg << "\n"; ++failed; } \
} while(0)

static void resetStat(uint32_t idx) {
    PerfManager::Instance().GetShard().stats[idx].reset();
    PerfManager::Instance().GetShard().stats[idx].slotIndex = idx;
    PerfManager::Instance().GetShard().stats[idx].level = static_cast<uint8_t>(PerfLevel::KEY_MODULE);
}

static void getAggregatedStat(uint32_t idx, PerfStat& result) {
    // 重置 result，避免累加脏值（调用方传入未初始化的 PerfStat 也安全）
    result.reset();
    result.slotIndex = idx;
    PerfShardManager& mgr = PerfManager::Instance().GetShardManager();
    uint32_t numCores = mgr.GetNumCores();
    LocalPerfShard* shards = mgr.GetShardArray();
    for (uint32_t c = 0; c < numCores; ++c) {
        const PerfStat& s = shards[c].stats[idx];
        result.backGoodCount += s.backGoodCount;
        result.backBadCount += s.backBadCount;
        result.backNotCount += s.backNotCount;
        result.tickCount += s.tickCount;
        result.totalTimeNs += s.totalTimeNs;
        if (s.minTimeNs < result.minTimeNs || result.minTimeNs == 0) result.minTimeNs = s.minTimeNs;
        if (s.maxTimeNs > result.maxTimeNs) result.maxTimeNs = s.maxTimeNs;
    }
}

static void ThreadWorker(int iterations, std::atomic<int>& totalGood, PerfKey key) {
    PerfPoint pt(key, PerfLevel::KEY_MODULE);
    for (int i = 0; i < iterations; ++i) {
        pt.Start();
        pt.End(0);
        totalGood++;
    }
}

int main() {
    std::cout << "=== User-Defined Auto Perf Test ===\n";
    std::cout << "(Using auto_perf.h - no manual enum or Init needed)\n\n";

    PerfManager::Instance().EnsureInitialized();
    if (!PerfManager::Instance().IsInitialized()) {
        std::cout << "spdiag shared memory is not available; skipping "
                     "counter assertions.\n";
        return 0;
    }

    auto sid = [](PerfKey k) -> uint32_t { return static_cast<uint32_t>(k); };

    // ============================================================
    std::cout << "1. Normal End (good return code)\n";
    // ============================================================
    {
        PerfPoint pt(PerfKey::USER_LOGIN, PerfLevel::KEY_MODULE);
        resetStat(sid(PerfKey::USER_LOGIN));
        pt.Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        pt.End(0);

        PerfStat s;
        getAggregatedStat(sid(PerfKey::USER_LOGIN), s);
        CHECK(s.backGoodCount > 0, "backGoodCount incremented");
        CHECK(s.backBadCount == 0, "backBadCount is 0");
        CHECK(s.backNotCount == 0, "backNotCount is 0");
        CHECK(s.tickCount > 0, "tickCount incremented");
        CHECK(s.totalTimeNs > 0, "totalTimeNs > 0");
        CHECK(s.minTimeNs <= s.maxTimeNs, "minTimeNs <= maxTimeNs");
    }

    // ============================================================
    std::cout << "\n2. Bad return code\n";
    // ============================================================
    {
        PerfPoint pt(PerfKey::USER_QUERY, PerfLevel::KEY_MODULE);
        resetStat(sid(PerfKey::USER_QUERY));
        pt.Start();
        pt.End(1);

        PerfStat s;
        getAggregatedStat(sid(PerfKey::USER_QUERY), s);
        CHECK(s.backBadCount > 0, "backBadCount incremented");
        CHECK(s.backGoodCount == 0, "backGoodCount is 0");
    }

    // ============================================================
    std::cout << "\n3. Abandon on destructor (no End call)\n";
    // ============================================================
    {
        PerfPoint ptOuter(PerfKey::USER_LOGOUT, PerfLevel::KEY_MODULE);
        resetStat(sid(PerfKey::USER_LOGOUT));
        {
            PerfPoint pt(PerfKey::USER_LOGOUT, PerfLevel::KEY_MODULE);
            pt.Start();
        } // destructor calls Abandon()

        PerfStat s;
        getAggregatedStat(sid(PerfKey::USER_LOGOUT), s);
        CHECK(s.backNotCount > 0, "backNotCount incremented by dtor");
        CHECK(s.tickCount > 0, "tickCount incremented");
    }

    // ============================================================
    std::cout << "\n4. Explicit Abandon()\n";
    // ============================================================
    {
        PerfPoint pt(PerfKey::ADMIN_REPORT, PerfLevel::KEY_MODULE);
        resetStat(sid(PerfKey::ADMIN_REPORT));
        pt.Start();
        pt.Abandon();

        PerfStat s;
        getAggregatedStat(sid(PerfKey::ADMIN_REPORT), s);
        CHECK(s.backNotCount > 0, "backNotCount incremented");
        CHECK(s.backGoodCount == 0, "backGoodCount is 0");
        CHECK(s.backBadCount == 0, "backBadCount is 0");
    }

    // ============================================================
    std::cout << "\n5. Level gating (IsLevelEnabled API)\n";
    // ============================================================
    {
        uint8_t gate = PerfManager::Instance().LevelGate();
        CHECK(PerfManager::Instance().IsLevelEnabled(PerfLevel::SUB_SYSTEM),
              "SubSystem(1) enabled");
        CHECK(PerfManager::Instance().IsLevelEnabled(PerfLevel::KEY_MODULE),
              "KeyModule(2) enabled");
        CHECK(!PerfManager::Instance().IsLevelEnabled(PerfLevel::MODULE) == (gate < 3),
              "Module(3) gate check");
        CHECK(!PerfManager::Instance().IsLevelEnabled(PerfLevel::DEBUG) == (gate < 4),
              "Debug(4) gate check");
    }

    // ============================================================
    std::cout << "\n6. Reuse PerfPoint (multiple Start/End pairs)\n";
    // ============================================================
    {
        PerfPoint pt(PerfKey::ADMIN_CONFIG, PerfLevel::KEY_MODULE);
        resetStat(sid(PerfKey::ADMIN_CONFIG));

        const int N = 5;
        for (int i = 0; i < N; ++i) {
            pt.Start();
            pt.End(0);
        }

        PerfStat s;
        getAggregatedStat(sid(PerfKey::ADMIN_CONFIG), s);
        CHECK(s.backGoodCount == static_cast<uint32_t>(N),
              "good count matches iteration count");
        CHECK(s.tickCount == static_cast<uint32_t>(N),
              "tick count matches iteration count");
    }

    // ============================================================
    std::cout << "\n7. Multiple keys in single scope\n";
    // ============================================================
    {
        PerfPoint pt1(PerfKey::USER_LOGIN, PerfLevel::KEY_MODULE);
        PerfPoint pt2(PerfKey::USER_QUERY, PerfLevel::KEY_MODULE);
        resetStat(sid(PerfKey::USER_LOGIN));
        resetStat(sid(PerfKey::USER_QUERY));

        pt1.Start();
        pt1.End(0);
        pt2.Start();
        pt2.End(0);

        PerfStat s1;
        getAggregatedStat(sid(PerfKey::USER_LOGIN), s1);
        PerfStat s2;
        getAggregatedStat(sid(PerfKey::USER_QUERY), s2);
        CHECK(s1.backGoodCount == 1, "slot 1 good count == 1");
        CHECK(s2.backGoodCount == 1, "slot 2 good count == 1");
    }

    // ============================================================
    std::cout << "\n8. Concurrent profiling (multi-threaded)\n";
    // ============================================================
    {
        PerfPoint initPt(PerfKey::USER_LOGIN, PerfLevel::KEY_MODULE);
        resetStat(sid(PerfKey::USER_LOGIN));

        std::atomic<int> totalGood{0};
        std::vector<std::thread> threads;
        const int numThreads = 4;
        const int iterationsPerThread = 100;

        for (int t = 0; t < numThreads; ++t) {
            threads.emplace_back(ThreadWorker, iterationsPerThread, std::ref(totalGood), PerfKey::USER_LOGIN);
        }
        for (auto& t : threads) {
            t.join();
        }

        PerfStat s;
        getAggregatedStat(sid(PerfKey::USER_LOGIN), s);
        CHECK(totalGood == numThreads * iterationsPerThread, "total iterations match");
        CHECK(s.backGoodCount == static_cast<uint32_t>(numThreads * iterationsPerThread),
              "concurrent good count == 400");
    }

    // ============================================================
    std::cout << "\n========================================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";
    std::cout << "========================================\n";

    PerfManager::Instance().Shutdown();
    return failed > 0 ? 1 : 0;
}
