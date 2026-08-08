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

#ifdef SPDIAG_ENABLE_PERFLOG

#include <gtest/gtest.h>
#include "spdiag/shm_layout.h"
#include "spdiag/perf_shard.h"

using namespace SpDiag;

TEST(PerfLogTest, PerfStatResetClearsPerfLogFields) {
    PerfStat stat;
    for (uint32_t i = 0; i < perflogTimestampCount; ++i) {
        stat.timestamps[i] = 100 + i;
        stat.perflogCosts[i] = 200 + i;
        stat.perflogThreadIds[i] = static_cast<uint16_t>(300 + i);
    }
    stat.perflogWritePos.store(42, std::memory_order_relaxed);

    stat.reset();

    for (uint32_t i = 0; i < perflogTimestampCount; ++i) {
        EXPECT_EQ(stat.timestamps[i], 0ULL) << "timestamps[" << i << "] not reset";
        EXPECT_EQ(stat.perflogCosts[i], 0ULL) << "costs[" << i << "] not reset";
        EXPECT_EQ(stat.perflogThreadIds[i], 0U) << "threadIds[" << i << "] not reset";
    }
    EXPECT_EQ(stat.perflogWritePos.load(std::memory_order_relaxed), 0U);
}

TEST(PerfLogTest, PerfStatCopyCopiesPerfLogFields) {
    PerfStat src;
    for (uint32_t i = 0; i < perflogTimestampCount; ++i) {
        src.timestamps[i] = 1000 + i * 10;
        src.perflogCosts[i] = 2000 + i * 10;
        src.perflogThreadIds[i] = static_cast<uint16_t>(3000 + i);
    }
    src.perflogWritePos.store(77, std::memory_order_relaxed);

    PerfStat dst;
    dst.reset();
    dst.copy(src);

    for (uint32_t i = 0; i < perflogTimestampCount; ++i) {
        EXPECT_EQ(dst.timestamps[i], src.timestamps[i]) << "timestamps[" << i << "] mismatch";
        EXPECT_EQ(dst.perflogCosts[i], src.perflogCosts[i]) << "costs[" << i << "] mismatch";
        EXPECT_EQ(dst.perflogThreadIds[i], src.perflogThreadIds[i]) << "threadIds[" << i << "] mismatch";
    }
    EXPECT_EQ(dst.perflogWritePos.load(std::memory_order_relaxed),
              src.perflogWritePos.load(std::memory_order_relaxed));
}

TEST(PerfLogTest, RingBufferWritePositionModulo) {
    PerfStat stat;
    stat.reset();

    for (uint32_t pos = 0; pos < 250; ++pos) {
        uint32_t idx = pos % perflogTimestampCount;
        stat.timestamps[idx] = pos;
        stat.perflogCosts[idx] = pos * 10;
        stat.perflogThreadIds[idx] = static_cast<uint16_t>(pos % 65536);
        stat.perflogWritePos.store(pos + 1, std::memory_order_relaxed);

        uint32_t expectedPos = pos + 1;
        EXPECT_EQ(stat.perflogWritePos.load(std::memory_order_relaxed), expectedPos);
        EXPECT_EQ(stat.timestamps[idx], static_cast<uint64_t>(pos));
        EXPECT_EQ(stat.perflogCosts[idx], static_cast<uint64_t>(pos * 10));
        EXPECT_EQ(stat.perflogThreadIds[idx], static_cast<uint16_t>(pos % 65536));
    }

    EXPECT_EQ(stat.timestamps[0], 200ULL);
    EXPECT_EQ(stat.timestamps[99], 199ULL);

    EXPECT_EQ(stat.perflogCosts[0], 2000ULL);
    EXPECT_EQ(stat.perflogCosts[99], 1990ULL);
}

TEST(PerfLogTest, RingBufferWrapOverwritesOldestEntries) {
    PerfStat stat;
    stat.reset();

    for (uint32_t pos = 0; pos < 100; ++pos) {
        uint32_t idx = pos % perflogTimestampCount;
        stat.timestamps[idx] = pos;
        stat.perflogCosts[idx] = pos;
        stat.perflogWritePos.store(pos + 1, std::memory_order_relaxed);
    }

    for (uint32_t idx = 0; idx < 100; ++idx) {
        EXPECT_EQ(stat.timestamps[idx], static_cast<uint64_t>(idx));
    }

    for (uint32_t pos = 100; pos < 150; ++pos) {
        uint32_t idx = pos % perflogTimestampCount;
        stat.timestamps[idx] = pos;
        stat.perflogCosts[idx] = pos;
        stat.perflogWritePos.store(pos + 1, std::memory_order_relaxed);
    }

    EXPECT_EQ(stat.timestamps[0], 100ULL);
    EXPECT_EQ(stat.perflogCosts[0], 100ULL);
    EXPECT_EQ(stat.timestamps[49], 149ULL);
    EXPECT_EQ(stat.perflogCosts[49], 149ULL);

    EXPECT_EQ(stat.timestamps[50], 50ULL);
    EXPECT_EQ(stat.perflogCosts[50], 50ULL);

    EXPECT_EQ(stat.perflogWritePos.load(std::memory_order_relaxed), 150U);
}

TEST(PerfLogTest, SnapshotAndResetPerfLogFields) {
    PerfShardManager mgr;
    mgr.Init(1);

    LocalPerfShard& shard = mgr.GetShard();
    for (int i = 0; i < 50; ++i) {
        PerfStat& stat = shard.stats[3];
        stat.timestamps[i] = i * 100 + 1;
        stat.perflogCosts[i] = i * 100 + 2;
        stat.perflogThreadIds[i] = static_cast<uint16_t>(i + 10);
        stat.perflogWritePos.store(50, std::memory_order_relaxed);
    }

    PerfStat& stat = shard.stats[3];
    stat.backGoodCount = 10;
    stat.totalTimeNs = 5000;

    LocalPerfShard snapshot;
    mgr.SnapshotAndReset(snapshot, shard);

    PerfStat& snapStat = snapshot.stats[3];
    EXPECT_EQ(snapStat.backGoodCount, 10U);
    EXPECT_EQ(snapStat.totalTimeNs, 5000ULL);

    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(snapStat.timestamps[i], static_cast<uint64_t>(i * 100 + 1))
            << "snapshot timestamp[" << i << "] mismatch";
        EXPECT_EQ(snapStat.perflogCosts[i], static_cast<uint64_t>(i * 100 + 2))
            << "snapshot cost[" << i << "] mismatch";
        EXPECT_EQ(snapStat.perflogThreadIds[i], static_cast<uint16_t>(i + 10))
            << "snapshot threadId[" << i << "] mismatch";
    }
    EXPECT_EQ(snapStat.perflogWritePos.load(std::memory_order_relaxed), 50U);

    EXPECT_EQ(shard.stats[3].backGoodCount, 0U);
    EXPECT_EQ(shard.stats[3].totalTimeNs, 0ULL);

    for (uint32_t i = 0; i < perflogTimestampCount; ++i) {
        EXPECT_EQ(shard.stats[3].timestamps[i], 0ULL);
        EXPECT_EQ(shard.stats[3].perflogCosts[i], 0ULL);
        EXPECT_EQ(shard.stats[3].perflogThreadIds[i], 0U);
    }
    EXPECT_EQ(shard.stats[3].perflogWritePos.load(std::memory_order_relaxed), 0U);
}

TEST(PerfLogTest, PerfStatCopyResetsCountersWhenMaxTimeChanges) {
    PerfStat stat;
    stat.reset();

    stat.minTimeNs = 0;
    stat.maxTimeNs = 1000;
    stat.backGoodCount = 5;
    stat.tickCount = 5;
    stat.totalTimeNs = 3000;

    PerfStat dst;
    dst.reset();
    dst.copy(stat);

    EXPECT_EQ(dst.backGoodCount, 5U);
    EXPECT_EQ(dst.tickCount, 5U);
    EXPECT_EQ(dst.totalTimeNs, 3000ULL);
    EXPECT_EQ(dst.minTimeNs, 0ULL);
    EXPECT_EQ(dst.maxTimeNs, 1000ULL);
}

TEST(PerfLogTest, PerfStatResetDoesNotAffectCountersWhenCopied) {
    PerfStat a;
    a.reset();
    a.backGoodCount = 10;
    a.totalTimeNs = 999;
    for (int i = 0; i < 10; ++i) {
        a.timestamps[i] = i;
        a.perflogCosts[i] = i * 2;
    }
    a.perflogWritePos.store(10, std::memory_order_relaxed);

    PerfStat b;
    b.reset();
    b.copy(a);

    a.reset();

    EXPECT_EQ(a.backGoodCount, 0U);
    EXPECT_EQ(a.totalTimeNs, 0ULL);
    EXPECT_EQ(b.backGoodCount, 10U);
    EXPECT_EQ(b.totalTimeNs, 999ULL);
}

TEST(PerfLogTest, LocalPerfShardCopyCopiesAllStats) {
    PerfShardManager mgr;
    mgr.Init(1);
    LocalPerfShard& shard = mgr.GetShard();

    shard.coreId = 5;
    for (uint32_t s = 0; s < MAX_PERF_SLOTS; ++s) {
        shard.stats[s].backGoodCount = s;
        shard.stats[s].totalTimeNs = s * 1000;
        shard.stats[s].timestamps[0] = s;
        shard.stats[s].perflogCosts[0] = s + 1;
    }

    LocalPerfShard copied;
    copied.copy(shard);

    EXPECT_EQ(copied.coreId, 5U);
    for (uint32_t s = 0; s < MAX_PERF_SLOTS; ++s) {
        EXPECT_EQ(copied.stats[s].backGoodCount, s);
        EXPECT_EQ(copied.stats[s].totalTimeNs, s * 1000ULL);
        EXPECT_EQ(copied.stats[s].timestamps[0], static_cast<uint64_t>(s));
        EXPECT_EQ(copied.stats[s].perflogCosts[0], static_cast<uint64_t>(s + 1));
    }
}

#endif
