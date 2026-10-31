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
#include "spdiag/perf_config.h"
#include "src/runtime/shm_reader.h"
#include "src/manager/manager.h"
#include "src/manager/aggregator.h"
#include "src/cli/cli_config.h"
#include "spdiag/shm_layout.h"
#include "test_helpers/spdiag_test_fixtures.h"

#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdio>

using namespace SpDiag;

class BoundaryIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        shmName = "/spdiag_test_bound";
        shmConfig.command = Cli::Command::START;
        shmConfig.shmName = shmName;
        shmConfig.numCores = 4;
        shmConfig.maxSlots = MAX_PERF_SLOTS;
        shmConfig.maxPrograms = 1;
        shmConfig.dumpIntervalMs = 100;
        (void)manager.StopShm(shmConfig);
        ASSERT_TRUE(manager.StartShm(shmConfig));
        spdiag_test::InitPerfManagerForTest(shmName, 4, RunMode::DEBUG, 100);
        ASSERT_TRUE(PerfManager::Instance().IsInitialized());
    }

    void TearDown() override {
        PerfManager::Instance().Shutdown();
        shmConfig.command = Cli::Command::STOP;
        (void)manager.StopShm(shmConfig);
    }

    std::string shmName;
    Cli::CliConfig shmConfig;
    SpDiagManager manager;
};

#ifdef SPDIAG_ENABLE_PERCENTILE
TEST(ConfigurableTopNIntegrationTest, ProducerRetainsConfiguredCapacity) {
    constexpr const char* kConfigShm = "/spdiag_test_topn_config";
    PerfManager::Instance().Shutdown();

    Cli::CliConfig startConfig;
    startConfig.command = Cli::Command::START;
    startConfig.shmName = kConfigShm;
    startConfig.numCores = 4;
    startConfig.maxSlots = MAX_PERF_SLOTS;
    startConfig.maxPrograms = 1;
    startConfig.dumpIntervalMs = 100;
    startConfig.perCoreTopN = 1000;

    SpDiagManager manager;
    Cli::CliConfig stopConfig = startConfig;
    stopConfig.command = Cli::Command::STOP;
    (void)manager.StopShm(stopConfig);
    ASSERT_TRUE(manager.StartShm(startConfig));

    struct Cleanup {
        SpDiagManager* manager;
        Cli::CliConfig stopConfig;
        ~Cleanup() {
            PerfManager::Instance().Shutdown();
            (void)manager->StopShm(stopConfig);
        }
    } cleanup{&manager, stopConfig};

    PerfConfig perfConfig;
    perfConfig.shmName = kConfigShm;
    perfConfig.mode = RunMode::DEBUG;
    perfConfig.levelGate = 4;
    perfConfig.dumpIntervalMs = 100;
    perfConfig.programName = "topn-config-test";
    perfConfig.slotCount =
        static_cast<uint32_t>(PerfKeyDefined::PERF_KEY_DEFINED_COUNT);
    PerfManager::Instance().Init(perfConfig);
    ASSERT_TRUE(PerfManager::Instance().IsInitialized());

    constexpr uint32_t kMeasurements = 20000;
    for (uint32_t i = 0; i < kMeasurements; ++i) {
        PerfPoint point(PerfKeyDefined::COMPUTE_MATMUL,
                        PerfLevel::SUB_SYSTEM);
        point.End(0);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    ShmReader reader;
    ASSERT_TRUE(reader.Open(kConfigShm));
    EXPECT_EQ(reader.Header().perCoreTopNCapacity, 1000u);
    ShardBuffer shards;
    ASSERT_TRUE(reader.SnapshotShards(shards));

    uint32_t retained = 0;
    uint64_t totalTicks = 0;
    const uint32_t slot = reader.ReadProgramRegistry()[0].baseSlot +
        static_cast<uint32_t>(PerfKeyDefined::COMPUTE_MATMUL);
    for (size_t core = 0; core < shards.size(); ++core) {
        const auto heap = shards.topN(core, slot);
        retained += heap.count == nullptr ? 0 : *heap.count;
        totalTicks += shards.stats(core)[slot].tickCount;
    }
    EXPECT_EQ(totalTicks, kMeasurements);
    EXPECT_GE(retained, 1000u);
    EXPECT_LE(retained, 4u * 1000u);

    Aggregator aggregator;
    aggregator.Aggregate(reader.ReadProgramRegistry(),
                         reader.Header().registeredPrograms,
                         reader.ReadKeyNames(), reader.ReadKeyModuleNames(),
                         shards, reader.Header().numCores);
    ASSERT_FALSE(aggregator.Results().empty());
    const auto result = std::find_if(
        aggregator.Results().begin(), aggregator.Results().end(),
        [=](const AggregatedStat& stat) {
            return stat.totalTickCount == kMeasurements;
        });
    ASSERT_NE(result, aggregator.Results().end());
    EXPECT_TRUE(result->hasP99);
    reader.Close();
}
#endif

TEST_F(BoundaryIntegrationTest, AllPredefinedKeys) {
    PerfKeyDefined allKeys[] = {
        PerfKeyDefined::COMPUTE_MATMUL,
        PerfKeyDefined::COMPUTE_PRIME,
        PerfKeyDefined::IO_IOTASK,
        PerfKeyDefined::PROCESS_WORK,
        PerfKeyDefined::PIPELINE_STAGE1,
        PerfKeyDefined::PIPELINE_STAGE2,
        PerfKeyDefined::PIPELINE_STAGE3,
        PerfKeyDefined::SYSTEM_SCHEDULER,
        PerfKeyDefined::MEMORY_ALLOC
    };

    for (auto key : allKeys) {
        PerfPoint pt(key, PerfLevel::SUB_SYSTEM);
        pt.End(0);
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
    EXPECT_GE(totalTicks, 9u);

    reader.Close();
}

TEST_F(BoundaryIntegrationTest, TopNHeapFilling) {
    const int measurements = 500;

    for (int i = 0; i < measurements; ++i) {
        PerfPoint pt(PerfKeyDefined::COMPUTE_MATMUL, PerfLevel::SUB_SYSTEM);

        volatile uint64_t dummy = 0;
        int loopBound = (i % 200) + 1;
        for (int j = 0; j < loopBound; ++j) {
            dummy += static_cast<uint64_t>(j) * static_cast<uint64_t>(j);
        }

        pt.End(0);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    ShmReader reader;
    ASSERT_TRUE(reader.Open(shmName));

    ShardBuffer shards;
    reader.SnapshotShards(shards);

#ifdef SPDIAG_ENABLE_PERCENTILE
    bool foundTopN = false;
    uint32_t statCount = static_cast<uint32_t>(PerfKeyDefined::PERF_KEY_DEFINED_COUNT);
    for (size_t s = 0; s < shards.size(); ++s) {
        const PerfStat* stats = shards.stats(s);
        for (uint32_t i = 0; i < statCount; ++i) {
            if (stats[i].tickCount > 0 && stats[i].slotIndex != 0) {
                const auto topN = shards.topN(s, i);
                ASSERT_NE(topN.count, nullptr);
                EXPECT_GT(*topN.count, 0u);
                if (*topN.count >= maxTopN) {
                    foundTopN = true;
                    for (uint32_t t = 0; t < *topN.count; ++t) {
                        EXPECT_GT(topN.values[t], 0u);
                    }
                }
            }
        }
    }

    if (!foundTopN) {
#endif
        uint64_t totalTicks = 0;
        uint32_t statCount2 = static_cast<uint32_t>(PerfKeyDefined::PERF_KEY_DEFINED_COUNT);
        for (size_t s = 0; s < shards.size(); ++s) {
            const PerfStat* stats = shards.stats(s);
            for (uint32_t i = 0; i < statCount2; ++i) {
                totalTicks += stats[i].tickCount;
            }
        }
        EXPECT_GT(totalTicks, 0u);
#ifdef SPDIAG_ENABLE_PERCENTILE
    }
#endif

    reader.Close();
}

TEST_F(BoundaryIntegrationTest, DumpDuringConcurrentReads) {
    const int readerThreads = 4;
    const int readDurationMs = 2000;
    std::atomic<bool> stopReaders{false};
    std::atomic<int> readSuccessCount{0};
    std::atomic<int> readFailCount{0};

    auto readerFunc = [&]() {
        ShmReader reader;
        if (!reader.Open(shmName)) {
            ++readFailCount;
            return;
        }

        while (!stopReaders.load(std::memory_order_relaxed)) {
            ShardBuffer shards;
            reader.SnapshotShards(shards);
            ++readSuccessCount;

            const ShmHeader& hdr = reader.Header();
            EXPECT_EQ(hdr.magic, shmMagic);
        }

        reader.Close();
    };

    std::vector<std::thread> readers;
    readers.reserve(readerThreads);
    for (int i = 0; i < readerThreads; ++i) {
        readers.emplace_back(readerFunc);
    }

    auto writerFunc = [&]() {
        for (int i = 0; i < 2000 && !stopReaders.load(std::memory_order_relaxed); ++i) {
            PerfPoint pt(PerfKeyDefined::PROCESS_WORK, PerfLevel::SUB_SYSTEM);
            volatile uint64_t dummy = 0;
            for (int j = 0; j < 500; ++j) {
                dummy += static_cast<uint64_t>(j);
            }
            pt.End(0);
        }
    };

    std::vector<std::thread> writers;
    writers.reserve(4);
    for (int i = 0; i < 4; ++i) {
        writers.emplace_back(writerFunc);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(readDurationMs));
    stopReaders.store(true, std::memory_order_relaxed);

    for (auto& w : writers) {
        w.join();
    }
    for (auto& r : readers) {
        r.join();
    }

    EXPECT_GT(readSuccessCount.load(), 0);
    EXPECT_EQ(readFailCount.load(), 0);
}

TEST_F(BoundaryIntegrationTest, PerfPointAbandon) {
    {
        PerfPoint pt(PerfKeyDefined::COMPUTE_PRIME, PerfLevel::SUB_SYSTEM);
    }

    {
        PerfPoint pt(PerfKeyDefined::COMPUTE_PRIME, PerfLevel::SUB_SYSTEM);
        pt.Abandon();
    }

    {
        PerfPoint pt(PerfKeyDefined::COMPUTE_PRIME, PerfLevel::SUB_SYSTEM);
        pt.End(0);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    ShmReader reader;
    ASSERT_TRUE(reader.Open(shmName));

    ShardBuffer shards;
    reader.SnapshotShards(shards);

    uint32_t statCount = static_cast<uint32_t>(PerfKeyDefined::PERF_KEY_DEFINED_COUNT);
    uint32_t totalNot = 0;
    uint32_t totalGood = 0;

    for (size_t s = 0; s < shards.size(); ++s) {
        const PerfStat* stats = shards.stats(s);
        for (uint32_t i = 0; i < statCount; ++i) {
            if (stats[i].tickCount > 0) {
                totalNot += stats[i].backNotCount;
                totalGood += stats[i].backGoodCount;
            }
        }
    }

    EXPECT_GT(totalNot, 0u);
    EXPECT_GT(totalGood, 0u);

    reader.Close();
}

TEST_F(BoundaryIntegrationTest, BadReturnCodeTracking) {
    const int goodCount = 10;
    const int badCount = 5;

    for (int i = 0; i < goodCount; ++i) {
        PerfPoint pt(PerfKeyDefined::IO_IOTASK, PerfLevel::SUB_SYSTEM);
        pt.End(0);
    }

    for (int i = 0; i < badCount; ++i) {
        PerfPoint pt(PerfKeyDefined::IO_IOTASK, PerfLevel::SUB_SYSTEM);
        pt.End(1);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    ShmReader reader;
    ASSERT_TRUE(reader.Open(shmName));

    ShardBuffer shards;
    reader.SnapshotShards(shards);

    uint32_t statCount = static_cast<uint32_t>(PerfKeyDefined::PERF_KEY_DEFINED_COUNT);
    uint32_t foundGood = 0;
    uint32_t foundBad = 0;

    for (size_t s = 0; s < shards.size(); ++s) {
        const PerfStat* stats = shards.stats(s);
        for (uint32_t i = 0; i < statCount; ++i) {
            if (stats[i].tickCount > 0) {
                foundGood += stats[i].backGoodCount;
                foundBad += stats[i].backBadCount;
            }
        }
    }

    EXPECT_EQ(foundGood, static_cast<uint32_t>(goodCount));
    EXPECT_EQ(foundBad, static_cast<uint32_t>(badCount));

    reader.Close();
}

TEST_F(BoundaryIntegrationTest, LevelGating) {
    constexpr const char* gateShm = "/spdiag_test_bound_gate";
    Cli::CliConfig gateConfig;
    gateConfig.command = Cli::Command::START;
    gateConfig.shmName = gateShm;
    gateConfig.numCores = 4;
    gateConfig.maxSlots = MAX_PERF_SLOTS;
    gateConfig.maxPrograms = 1;
    gateConfig.dumpIntervalMs = 100;
    gateConfig.levelGate = 2;
    SpDiagManager gateManager;
    (void)gateManager.StopShm(gateConfig);
    ASSERT_TRUE(gateManager.StartShm(gateConfig));
    struct GateCleanup {
        SpDiagManager* manager;
        Cli::CliConfig config;
        ~GateCleanup() {
            PerfManager::Instance().Shutdown();
            config.command = Cli::Command::STOP;
            (void)manager->StopShm(config);
        }
    } gateCleanup{&gateManager, gateConfig};

    PerfConfig config;
    config.shmName = gateShm;
    config.mode = RunMode::RELEASE;
    config.levelGate = 2;
    config.dumpIntervalMs = 100;
    PerfManager::Instance().Shutdown();
    PerfManager::Instance().Init(config);

    for (int i = 0; i < 10; ++i) {
        PerfPoint pt(PerfKeyDefined::COMPUTE_MATMUL, PerfLevel::SUB_SYSTEM);
        pt.End(0);
    }

    for (int i = 0; i < 10; ++i) {
        PerfPoint pt(PerfKeyDefined::COMPUTE_PRIME, PerfLevel::DEBUG);
        pt.End(0);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    ShmReader reader;
    ASSERT_TRUE(reader.Open(gateShm));

    ShardBuffer shards;
    reader.SnapshotShards(shards);

    uint32_t statCount = static_cast<uint32_t>(PerfKeyDefined::PERF_KEY_DEFINED_COUNT);
    uint32_t subSystemTicks = 0;
    uint32_t debugTicks = 0;

    for (size_t s = 0; s < shards.size(); ++s) {
        const PerfStat* stats = shards.stats(s);
        uint32_t matmulIdx = static_cast<uint32_t>(PerfKeyDefined::COMPUTE_MATMUL);
        uint32_t primeIdx = static_cast<uint32_t>(PerfKeyDefined::COMPUTE_PRIME);

        if (matmulIdx < statCount) {
            subSystemTicks += stats[matmulIdx].tickCount;
        }
        if (primeIdx < statCount) {
            debugTicks += stats[primeIdx].tickCount;
        }
    }

    EXPECT_GT(subSystemTicks, 0u);
    EXPECT_EQ(debugTicks, 0u);

    reader.Close();

    PerfManager::Instance().Shutdown();

    PerfConfig resetConfig;
    resetConfig.shmName = shmName;
    resetConfig.mode = RunMode::DEBUG;
    resetConfig.levelGate = 4;
    resetConfig.dumpIntervalMs = 100;
    PerfManager::Instance().Init(resetConfig);
}
