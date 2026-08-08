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

#include <gtest/gtest.h>
#include "spdiag/perf_manager.h"
#include "spdiag/perf_point.h"
#include "spdiag/perf_key_defined.h"
#include "spdiag/perf_level.h"
#include "src/runtime/shm_reader.h"
#include "spdiag/shm_layout.h"
#include "test_helpers/spdiag_test_fixtures.h"

#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <cstring>

using namespace SpDiag;

class ConcurrencyIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        shmName = "/spdiag_test_conc";
        spdiag_test::InitPerfManagerForTest(shmName, 4, RunMode::DEBUG, 100);
        SPDIAG_SKIP_IF_NO_SHM();
    }

    void TearDown() override {
        PerfManager::Instance().Shutdown();
    }

    std::string shmName;
};

static void TightLoopWorker(int, int iterations) {
    for (int i = 0; i < iterations; ++i) {
        PerfPoint pt(PerfKeyDefined::COMPUTE_PRIME, PerfLevel::SUB_SYSTEM);
        volatile uint64_t dummy = 0;
        for (int j = 0; j < 100; ++j) {
            dummy += static_cast<uint64_t>(j);
        }
        pt.End(0);
    }
}

TEST_F(ConcurrencyIntegrationTest, NoCrashUnderHighConcurrency) {
    const int numThreads = 16;
    const int iterationsPerThread = 5000;

    std::vector<std::thread> workers;
    workers.reserve(numThreads);
    for (int i = 0; i < numThreads; ++i) {
        workers.emplace_back(TightLoopWorker, i, iterationsPerThread);
    }
    for (auto& w : workers) {
        w.join();
    }

    SUCCEED();
}

TEST_F(ConcurrencyIntegrationTest, StatsConsistentAfterConcurrency) {
    const int numThreads = 16;
    const int iterationsPerThread = 2000;

    std::vector<std::thread> workers;
    workers.reserve(numThreads);
    for (int i = 0; i < numThreads; ++i) {
        workers.emplace_back(TightLoopWorker, i, iterationsPerThread);
    }
    for (auto& w : workers) {
        w.join();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    ShmReader reader;
    ASSERT_TRUE(reader.Open(shmName));

    ShardBuffer shards;
    reader.SnapshotShards(shards);

    uint64_t totalTicks = 0;
    uint64_t totalGood = 0;
    uint64_t totalBad = 0;
    uint64_t totalNot = 0;

    uint32_t statCount = static_cast<uint32_t>(PerfKeyDefined::PERF_KEY_DEFINED_COUNT);
    for (size_t s = 0; s < shards.size(); ++s) {
        const PerfStat* stats = shards.stats(s);
        for (uint32_t i = 0; i < statCount; ++i) {
            uint32_t ticks = stats[i].tickCount;
            if (ticks == 0) continue;

            totalTicks += ticks;
            totalGood += stats[i].backGoodCount;
            totalBad += stats[i].backBadCount;
            totalNot += stats[i].backNotCount;

            EXPECT_EQ(stats[i].backGoodCount + stats[i].backBadCount + stats[i].backNotCount, ticks);
        }
    }

    EXPECT_GT(totalTicks, 0u);
    EXPECT_EQ(totalGood + totalBad + totalNot, totalTicks);

    reader.Close();
}

TEST_F(ConcurrencyIntegrationTest, MultiplePerfKeysConcurrent) {
    const int numThreads = 8;
    const int iterationsPerThread = 1000;

    auto mixedWorker = [](int, int iters) {
        for (int i = 0; i < iters; ++i) {
            {
                PerfPoint pt(PerfKeyDefined::PIPELINE_STAGE1, PerfLevel::KEY_MODULE);
                volatile int x = i * i;
                (void)x;
                pt.End(0);
            }
            {
                PerfPoint pt(PerfKeyDefined::PIPELINE_STAGE2, PerfLevel::MODULE);
                volatile int y = i + i;
                (void)y;
                pt.End(0);
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(numThreads);
    for (int i = 0; i < numThreads; ++i) {
        workers.emplace_back(mixedWorker, i, iterationsPerThread);
    }
    for (auto& w : workers) {
        w.join();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    ShmReader reader;
    ASSERT_TRUE(reader.Open(shmName));

    ShardBuffer shards;
    reader.SnapshotShards(shards);

    uint64_t totalTicks = 0;
    uint32_t statCount = static_cast<uint32_t>(PerfKeyDefined::PERF_KEY_DEFINED_COUNT);
    for (size_t s = 0; s < shards.size(); ++s) {
        const PerfStat* stats = shards.stats(s);
        for (uint32_t i = 0; i < statCount; ++i) {
            totalTicks += stats[i].tickCount;
        }
    }

    uint64_t expectedMin = static_cast<uint64_t>(numThreads) * iterationsPerThread * 2;
    EXPECT_GE(totalTicks, expectedMin / 2);

    reader.Close();
}

TEST_F(ConcurrencyIntegrationTest, DifferentLevelsConcurrent) {
    const int numThreads = 8;
    const int iterationsPerThread = 500;

    auto levelWorker = [](int threadId, PerfLevel level, int iters) {
        PerfKeyDefined key = (threadId % 2 == 0)
            ? PerfKeyDefined::SYSTEM_SCHEDULER
            : PerfKeyDefined::MEMORY_ALLOC;

        for (int i = 0; i < iters; ++i) {
            PerfPoint pt(key, level);
            pt.End(0);
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(numThreads);

    PerfLevel levels[] = {PerfLevel::SUB_SYSTEM, PerfLevel::KEY_MODULE, PerfLevel::MODULE, PerfLevel::DEBUG};
    for (int i = 0; i < numThreads; ++i) {
        workers.emplace_back(levelWorker, i, levels[i % 4], iterationsPerThread);
    }
    for (auto& w : workers) {
        w.join();
    }

    SUCCEED();
}
