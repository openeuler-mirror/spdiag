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

class NormalIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        shmName = "/spdiag_test_normal";
        spdiag_test::InitPerfManagerForTest(shmName, 4, RunMode::DEBUG, 200);
        SPDIAG_SKIP_IF_NO_SHM();
    }

    void TearDown() override {
        PerfManager::Instance().Shutdown();
    }

    std::string shmName;
};

static void WorkerNormal(int, int iterations) {
    for (int i = 0; i < iterations; ++i) {
        {
            PerfPoint pt(PerfKeyDefined::COMPUTE_MATMUL, PerfLevel::SUB_SYSTEM);
            volatile uint64_t sum = 0;
            for (int j = 0; j < 5000; ++j) {
                sum += static_cast<uint64_t>(j) * 3;
            }
            pt.End(0);
        }

        {
            PerfPoint pt(PerfKeyDefined::IO_IOTASK, PerfLevel::KEY_MODULE);
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            pt.End(0);
        }

        {
            PerfPoint pt(PerfKeyDefined::PROCESS_WORK, PerfLevel::MODULE);
            volatile double val = 0.0;
            for (int j = 0; j < 1000; ++j) {
                val += j * 0.001;
            }
            pt.End(0);
        }
    }
}

TEST_F(NormalIntegrationTest, StatsPresentAndNonZero) {
    const int numThreads = 4;
    const int iterationsPerThread = 50;

    std::vector<std::thread> workers;
    workers.reserve(numThreads);
    for (int i = 0; i < numThreads; ++i) {
        workers.emplace_back(WorkerNormal, i, iterationsPerThread);
    }
    for (auto& w : workers) {
        w.join();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    ShmReader reader;
    ASSERT_TRUE(reader.Open(shmName));

    const ShmHeader& hdr = reader.Header();
    EXPECT_EQ(hdr.magic, shmMagic);
    EXPECT_GT(hdr.numCores, 0u);

    ShardBuffer shards;
    bool consistent = reader.SnapshotShards(shards);
    EXPECT_TRUE(consistent);

    uint64_t totalTicks = 0;
    uint32_t statCount = static_cast<uint32_t>(PerfKeyDefined::PERF_KEY_DEFINED_COUNT);
    for (size_t s = 0; s < shards.size(); ++s) {
        const PerfStat* stats = shards.stats(s);
        for (uint32_t i = 0; i < statCount; ++i) {
            totalTicks += stats[i].tickCount;
        }
    }
    EXPECT_GT(totalTicks, 0u);

    reader.Close();
}

TEST_F(NormalIntegrationTest, TimingDataReasonable) {
    WorkerNormal(0, 20);

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    ShmReader reader;
    ASSERT_TRUE(reader.Open(shmName));

    ShardBuffer shards;
    reader.SnapshotShards(shards);

    bool foundTiming = false;
    uint32_t statCount = static_cast<uint32_t>(PerfKeyDefined::PERF_KEY_DEFINED_COUNT);
    for (size_t s = 0; s < shards.size(); ++s) {
        const PerfStat* stats = shards.stats(s);
        for (uint32_t i = 0; i < statCount; ++i) {
            if (stats[i].tickCount > 0) {
                EXPECT_GT(stats[i].totalTimeNs, 0u);
                EXPECT_GT(stats[i].minTimeNs, 0u);
                EXPECT_GE(stats[i].maxTimeNs, stats[i].minTimeNs);
                foundTiming = true;
            }
        }
    }
    EXPECT_TRUE(foundTiming);

    reader.Close();
}

TEST_F(NormalIntegrationTest, GoodBadCountConsistent) {
    WorkerNormal(0, 30);

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    ShmReader reader;
    ASSERT_TRUE(reader.Open(shmName));

    ShardBuffer shards;
    reader.SnapshotShards(shards);

    uint32_t statCount = static_cast<uint32_t>(PerfKeyDefined::PERF_KEY_DEFINED_COUNT);
    for (size_t s = 0; s < shards.size(); ++s) {
        const PerfStat* stats = shards.stats(s);
        for (uint32_t i = 0; i < statCount; ++i) {
            uint32_t ticks = stats[i].tickCount;
            if (ticks > 0) {
                uint32_t good = stats[i].backGoodCount;
                uint32_t bad = stats[i].backBadCount;
                uint32_t notCount = stats[i].backNotCount;
                EXPECT_EQ(good + bad + notCount, ticks);
            }
        }
    }

    reader.Close();
}
