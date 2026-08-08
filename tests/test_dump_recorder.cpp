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
#include "src/manager/dump_recorder.h"
#include <cstdio>

static SpDiag::DumpRecord MakeRecord(uint64_t ts) {
    SpDiag::DumpRecord rec;
    rec.timestampNs = ts;
    return rec;
}

static SpDiag::DumpRecord MakeRecordWithStat(uint64_t ts) {
    SpDiag::DumpRecord rec;
    rec.timestampNs = ts;

    SpDiag::DumpStat stat;
    stat.globalSlotIndex = 42;
    stat.moduleName = "TestModule";
    stat.pointName = "TestPoint";
    stat.level = 3;
    stat.deltaTickCount = 100;
    stat.deltaGoodCount = 80;
    stat.deltaBadCount = 15;
    stat.deltaNotCount = 5;
    stat.deltaTimeNs = 50000;
    stat.minTimeNs = 10;
    stat.maxTimeNs = 9000;
#ifdef SPDIAG_ENABLE_PERCENTILE
    stat.topN = {9000, 8000, 7000};
    stat.p99Ns = 5000;
    stat.p999Ns = 7000;
    stat.p9999Ns = 8500;
    stat.hasP99 = true;
    stat.hasP999 = true;
    stat.hasP9999 = false;
#endif

    rec.stats.push_back(stat);
    return rec;
}

TEST(DumpRecorderTest, RecordAndRetrieve) {
    SpDiag::DumpRecorder rec(3);

    rec.Record(MakeRecord(100));
    rec.Record(MakeRecord(200));
    rec.Record(MakeRecord(300));

    EXPECT_EQ(rec.Count(), 3u);
    EXPECT_EQ(rec.GetRecord(0).timestampNs, 300u);
    EXPECT_EQ(rec.GetRecord(1).timestampNs, 200u);
    EXPECT_EQ(rec.GetRecord(2).timestampNs, 100u);
}

TEST(DumpRecorderTest, RingBufferOverflow) {
    SpDiag::DumpRecorder rec(3);

    rec.Record(MakeRecord(100));
    rec.Record(MakeRecord(200));
    rec.Record(MakeRecord(300));
    rec.Record(MakeRecord(400));
    rec.Record(MakeRecord(500));

    EXPECT_EQ(rec.Count(), 3u);
    EXPECT_EQ(rec.GetRecord(0).timestampNs, 500u);
    EXPECT_EQ(rec.GetRecord(1).timestampNs, 400u);
    EXPECT_EQ(rec.GetRecord(2).timestampNs, 300u);
}

TEST(DumpRecorderTest, SaveAndLoad) {
    const std::string tmpPath = "dump_recorder_test_tmp.bin";

    SpDiag::DumpRecorder rec1(5);
    rec1.Record(MakeRecordWithStat(1000));
    rec1.Record(MakeRecordWithStat(2000));

    ASSERT_TRUE(rec1.SaveToFile(tmpPath));

    SpDiag::DumpRecorder rec2(5);
    ASSERT_TRUE(rec2.LoadFromFile(tmpPath));

    EXPECT_EQ(rec2.Count(), rec1.Count());
    ASSERT_EQ(rec2.GetRecord(0).stats.size(), 1u);

    const SpDiag::DumpStat& orig = rec1.GetRecord(0).stats[0];
    const SpDiag::DumpStat& loaded = rec2.GetRecord(0).stats[0];

    EXPECT_EQ(loaded.globalSlotIndex, orig.globalSlotIndex);
    EXPECT_EQ(loaded.moduleName, orig.moduleName);
    EXPECT_EQ(loaded.pointName, orig.pointName);
    EXPECT_EQ(loaded.level, orig.level);
    EXPECT_EQ(loaded.deltaTickCount, orig.deltaTickCount);
    EXPECT_EQ(loaded.deltaGoodCount, orig.deltaGoodCount);
    EXPECT_EQ(loaded.deltaBadCount, orig.deltaBadCount);
    EXPECT_EQ(loaded.deltaNotCount, orig.deltaNotCount);
    EXPECT_EQ(loaded.deltaTimeNs, orig.deltaTimeNs);
    EXPECT_EQ(loaded.minTimeNs, orig.minTimeNs);
    EXPECT_EQ(loaded.maxTimeNs, orig.maxTimeNs);
#ifdef SPDIAG_ENABLE_PERCENTILE
    EXPECT_EQ(loaded.topN, orig.topN);
    EXPECT_EQ(loaded.p99Ns, orig.p99Ns);
    EXPECT_EQ(loaded.p999Ns, orig.p999Ns);
    EXPECT_EQ(loaded.p9999Ns, orig.p9999Ns);
    EXPECT_EQ(loaded.hasP99, orig.hasP99);
    EXPECT_EQ(loaded.hasP999, orig.hasP999);
    EXPECT_EQ(loaded.hasP9999, orig.hasP9999);
#endif

    std::remove(tmpPath.c_str());
}

TEST(DumpRecorderTest, SetCapacity) {
    SpDiag::DumpRecorder rec(5);

    rec.Record(MakeRecord(100));
    rec.Record(MakeRecord(200));
    rec.Record(MakeRecord(300));
    rec.Record(MakeRecord(400));
    rec.Record(MakeRecord(500));

    rec.SetCapacity(3);

    EXPECT_EQ(rec.Capacity(), 3u);
    EXPECT_EQ(rec.Count(), 3u);
    EXPECT_EQ(rec.GetRecord(0).timestampNs, 500u);
    EXPECT_EQ(rec.GetRecord(1).timestampNs, 400u);
    EXPECT_EQ(rec.GetRecord(2).timestampNs, 300u);
}
