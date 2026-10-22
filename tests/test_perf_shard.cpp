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
#include <algorithm>
#include <atomic>
#include <functional>
#include <thread>
#include "spdiag/perf_shard.h"
#include "spdiag/shm_layout.h"

TEST(PerfShardTest, InitAndNumCores) {
    SpDiag::PerfShardManager mgr;
    mgr.Init(4);
    EXPECT_EQ(mgr.GetNumCores(), 4u);
}

TEST(PerfShardTest, ShardArrayAndCoreId) {
    SpDiag::PerfShardManager mgr;
    mgr.Init(4);

    SpDiag::LocalPerfShard* arr = mgr.GetShardArray();
    ASSERT_NE(arr, nullptr);
    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_EQ(arr[i].coreId, i);
    }
}

TEST(PerfShardTest, DirectStatAccess) {
    SpDiag::PerfShardManager mgr;
    mgr.Init(2);

    SpDiag::LocalPerfShard& shard = mgr.GetShard();
    shard.stats[5].backGoodCount = 42;
    shard.stats[5].slotIndex = 5;

    EXPECT_EQ(shard.stats[5].backGoodCount, 42u);
    EXPECT_EQ(shard.stats[5].slotIndex, 5u);
}

TEST(PerfShardTest, LocalPerfShardReset) {
    SpDiag::PerfShardManager mgr;
    mgr.Init(1);

    SpDiag::LocalPerfShard& shard = mgr.GetShard();
    shard.coreId = 99;
    shard.stats[0].backGoodCount = 100;
    shard.stats[3].tickCount = 50;
    shard.stats[3].totalTimeNs = 999;

    shard.reset();
    EXPECT_EQ(shard.coreId, 0u);
    EXPECT_EQ(shard.stats[0].backGoodCount, 0u);
    EXPECT_EQ(shard.stats[3].tickCount, 0u);
    EXPECT_EQ(shard.stats[3].totalTimeNs, 0u);
}

TEST(PerfShardTest, LocalPerfShardCopy) {
    SpDiag::LocalPerfShard src{};
    src.coreId = 7;
    src.stats[2].backGoodCount = 33;
    src.stats[2].tickCount = 33;
    src.stats[2].totalTimeNs = 1000;

    SpDiag::LocalPerfShard dst{};
    dst.copy(src);
    EXPECT_EQ(dst.coreId, 7u);
    EXPECT_EQ(dst.stats[2].backGoodCount, 33u);
    EXPECT_EQ(dst.stats[2].tickCount, 33u);
    EXPECT_EQ(dst.stats[2].totalTimeNs, 1000u);
}

TEST(PerfShardTest, SnapshotAndReset) {
    SpDiag::PerfShardManager mgr;
    ASSERT_TRUE(mgr.Init(1, 4, SpDiag::defaultPerCoreTopN));

    SpDiag::LocalPerfShard& shard = mgr.GetShard();
    shard.stats[1].backGoodCount = 10;
    shard.stats[1].tickCount = 10;
#ifdef SPDIAG_ENABLE_PERCENTILE
    mgr.InsertTopNValue(shard, 1, 100);
#endif

    SpDiag::LocalPerfShard snapshot{};
    mgr.SnapshotAndReset(snapshot, shard);

    EXPECT_EQ(snapshot.stats[1].backGoodCount, 10u);
    EXPECT_EQ(snapshot.stats[1].tickCount, 10u);
    EXPECT_EQ(shard.stats[1].backGoodCount, 0u);
    EXPECT_EQ(shard.stats[1].tickCount, 0u);
#ifdef SPDIAG_ENABLE_PERCENTILE
    EXPECT_EQ(snapshot.stats[1].topNCount, 1u);
    EXPECT_EQ(mgr.GetTopN(shard, 1).values[0].load(), 100u);
    EXPECT_EQ(mgr.GetTopN(shard, 1).count->load(
                  std::memory_order_relaxed), 1u);
#endif
}

#ifdef SPDIAG_ENABLE_PERCENTILE
TEST(PerfShardTest, PerCoreTopNIsCumulativeAcrossSnapshots) {
    for (uint32_t capacity : {100U, 1000U, 10000U}) {
        SpDiag::PerfShardManager mgr;
        ASSERT_TRUE(mgr.Init(1, 4, capacity));
        SpDiag::LocalPerfShard& shard = mgr.GetShard();
        for (uint64_t value = 1; value <= capacity; ++value) {
            mgr.InsertTopNValue(shard, 1, value);
        }
        std::vector<uint64_t> snapshotTopN(
            static_cast<size_t>(4) * capacity);
        std::vector<uint16_t> counts(4);
        mgr.SnapshotTopN(snapshotTopN.data(), counts.data(), shard);
        EXPECT_EQ(counts[1], capacity);
        EXPECT_EQ(snapshotTopN[capacity], 1u);
        EXPECT_EQ(mgr.GetTopN(shard, 1).count->load(
                      std::memory_order_relaxed), capacity);
        for (uint64_t value = capacity + 1;
             value <= static_cast<uint64_t>(capacity) * 2; ++value) {
            mgr.InsertTopNValue(shard, 1, value);
        }
        mgr.SnapshotTopN(snapshotTopN.data(), counts.data(), shard);
        EXPECT_EQ(counts[1], capacity);
        EXPECT_EQ(snapshotTopN[capacity], capacity + 1);
    }
}
TEST(PerfShardTest, RestoresRuntimeTopNFromShmCandidates) {
    constexpr uint32_t capacity = 1000;
    SpDiag::PerfShardManager mgr;
    ASSERT_TRUE(mgr.Init(1, 2, capacity));
    SpDiag::LocalPerfShard& shard = mgr.GetShard();

    std::vector<uint64_t> source(capacity);
    for (uint32_t i = 0; i < capacity; ++i) {
        source[i] = capacity - i;
    }
    mgr.RestoreTopN(shard, 1,
                    {source.data(), capacity, capacity});

    auto restored = mgr.GetTopN(shard, 1);
    ASSERT_EQ(restored.count->load(), capacity);
    std::vector<uint64_t> values(capacity);
    for (uint32_t i = 0; i < capacity; ++i) values[i] = restored.values[i].load();
    std::sort(values.begin(), values.end());
    EXPECT_EQ(values.front(), 1u);
    EXPECT_EQ(values.back(), capacity);
}
TEST(PerfShardTest, PerCoreTopNConcurrentSnapshotConverges) {
    constexpr uint32_t capacity = 1000;
    constexpr uint64_t samples = 50000;
    SpDiag::PerfShardManager mgr;
    ASSERT_TRUE(mgr.Init(1, 2, capacity));
    SpDiag::LocalPerfShard& shard = mgr.GetShard();
    std::atomic<bool> done{false};
    uint32_t snapshots = 0;

    std::thread producer([&]() {
        for (uint64_t value = 1; value <= samples; ++value) {
            mgr.InsertTopNValue(shard, 1, value);
            if ((value & 0xff) == 0) {
                std::this_thread::yield();
            }
        }
        done.store(true, std::memory_order_release);
    });

    std::vector<uint64_t> snapshotTopN(2u * capacity);
    std::vector<uint16_t> counts(2);
    while (!done.load(std::memory_order_acquire)) {
        mgr.SnapshotTopN(snapshotTopN.data(), counts.data(), shard);
        EXPECT_LE(counts[1], capacity);
        ++snapshots;
    }
    producer.join();
    mgr.SnapshotTopN(snapshotTopN.data(), counts.data(), shard);
    EXPECT_GT(snapshots, 0u);
    ASSERT_EQ(counts[1], capacity);
    std::sort(snapshotTopN.begin() + capacity, snapshotTopN.end());
    EXPECT_EQ(snapshotTopN[capacity], samples - capacity + 1);
    EXPECT_EQ(snapshotTopN.back(), samples);
}
#endif
