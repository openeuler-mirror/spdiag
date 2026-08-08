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
#include "spdiag/shm_layout.h"
#include <cstring>

TEST(ShmLayoutTest, PerfStatAlignment) {
    EXPECT_EQ(alignof(SpDiag::PerfStat), 64u);
}

TEST(ShmLayoutTest, ShmMagic) {
    EXPECT_EQ(SpDiag::shmMagic, 0x50455246u);
}

TEST(ShmLayoutTest, Constants) {
    EXPECT_EQ(SpDiag::maxProgramsDefault, 8u);
#ifdef SPDIAG_ENABLE_PERCENTILE
    EXPECT_EQ(SpDiag::maxTopN, 100u);
#endif
    EXPECT_EQ(SpDiag::nameLen, 64u);
}

TEST(ShmLayoutTest, ProgramEntryFields) {
    SpDiag::ProgramEntry entry{};
    EXPECT_EQ(std::strlen(entry.name), 0u);
    EXPECT_EQ(entry.baseSlot, 0u);
    EXPECT_EQ(entry.slotCount, 0u);
    EXPECT_EQ(entry.numCores, 0u);
    EXPECT_EQ(entry.shardOffset, 0u);
    EXPECT_EQ(entry.pid, 0);
    EXPECT_EQ(entry.active, 0);
}

TEST(ShmLayoutTest, ProgramEntrySetName) {
    SpDiag::ProgramEntry entry{};
    std::strncpy(entry.name, "MyApp", 63);
    entry.name[63] = '\0';
    entry.baseSlot = 10;
    entry.slotCount = 9;
    entry.numCores = 4;
    entry.shardOffset = 40;
    entry.pid = 1234;
    entry.active = 1;

    EXPECT_STREQ(entry.name, "MyApp");
    EXPECT_EQ(entry.baseSlot, 10u);
    EXPECT_EQ(entry.slotCount, 9u);
    EXPECT_EQ(entry.numCores, 4u);
    EXPECT_EQ(entry.shardOffset, 40u);
    EXPECT_EQ(entry.pid, 1234);
    EXPECT_EQ(entry.active, 1);
}

TEST(ShmLayoutTest, ShmHeaderV3Fields) {
    SpDiag::ShmHeader hdr{};
    EXPECT_EQ(hdr.version, 3u);
    EXPECT_EQ(hdr.magic, 0u);
    EXPECT_EQ(hdr.maxSlots, 0u);
    EXPECT_EQ(hdr.maxPrograms, 0u);
    EXPECT_EQ(hdr.registeredPrograms, 0u);
    EXPECT_EQ(hdr.numCores, 0u);
    EXPECT_EQ(hdr.registryOffset, size_t(0));
    EXPECT_EQ(hdr.keyNamesOffset, size_t(0));
    EXPECT_EQ(hdr.keyModuleNamesOffset, size_t(0));
    EXPECT_EQ(hdr.activeBitmapOffset, size_t(0));
    EXPECT_EQ(hdr.shardsOffset, size_t(0));
    EXPECT_EQ(hdr.globalAccumOffset, size_t(0));
    EXPECT_EQ(hdr.globalAccumCount, 0u);
}

TEST(ShmLayoutTest, GlobalAccumStruct) {
    SpDiag::GlobalAccum a{};
    EXPECT_EQ(alignof(SpDiag::GlobalAccum), 64u);
    EXPECT_EQ(a.goodCount.load(), 0u);
    EXPECT_EQ(a.tickCount.load(), 0u);
    EXPECT_EQ(a.minTimeNs.load(), 0u);
    EXPECT_EQ(SpDiag::MAX_GLOBAL_POINTS, SpDiag::MAX_PERF_SLOTS);
}

TEST(ShmLayoutTest, ShmHeaderInit) {
    SpDiag::ShmHeader hdr{};
    hdr.magic = SpDiag::shmMagic;
    hdr.version = 3;
    hdr.mode = 1;
    hdr.levelGate = 4;
    hdr.maxSlots = 1024;
    hdr.maxPrograms = 8;
    hdr.registeredPrograms = 2;
    hdr.numCores = 4;
    hdr.dumpIntervalMs = 1000;
    hdr.shmTotalSize = 99999;
    hdr.registryOffset = sizeof(SpDiag::ShmHeader);
    hdr.keyNamesOffset = sizeof(SpDiag::ShmHeader) + 8 * sizeof(SpDiag::ProgramEntry);
    hdr.shardsOffset = hdr.keyModuleNamesOffset + 1024 * SpDiag::nameLen;

    EXPECT_EQ(hdr.magic, SpDiag::shmMagic);
    EXPECT_EQ(hdr.registeredPrograms, 2u);
    EXPECT_GT(hdr.keyNamesOffset, hdr.registryOffset);
    EXPECT_GT(hdr.shardsOffset, hdr.keyNamesOffset);
}

TEST(ShmLayoutTest, PerfStatSlotIndex) {
    SpDiag::PerfStat stat{};
    EXPECT_EQ(stat.slotIndex, 0u);
    stat.slotIndex = 42;
    EXPECT_EQ(stat.slotIndex, 42u);
}

TEST(ShmLayoutTest, LocalPerfShardReset) {
    SpDiag::LocalPerfShard shard{};
    shard.coreId = 1;
    shard.stats[0].backGoodCount = 100;
    shard.stats[5].tickCount = 50;

    shard.reset();
    EXPECT_EQ(shard.coreId, 0u);
    EXPECT_EQ(shard.stats[0].backGoodCount, 0u);
    EXPECT_EQ(shard.stats[5].tickCount, 0u);
}

TEST(ShmLayoutTest, KeyNamesLayoutRoundTrip) {
    static constexpr uint32_t SLOTS = 16;
    static constexpr size_t BUF_SIZE =
        sizeof(SpDiag::ShmHeader) +
        SpDiag::maxProgramsDefault * sizeof(SpDiag::ProgramEntry) +
        SLOTS * SpDiag::nameLen +
        SLOTS * SpDiag::nameLen;

    std::vector<uint8_t> buffer(BUF_SIZE, 0);
    SpDiag::ShmHeader* hdr = reinterpret_cast<SpDiag::ShmHeader*>(buffer.data());
    hdr->registryOffset = sizeof(SpDiag::ShmHeader);
    hdr->keyNamesOffset = hdr->registryOffset + SpDiag::maxProgramsDefault * sizeof(SpDiag::ProgramEntry);
    hdr->keyModuleNamesOffset = hdr->keyNamesOffset + SLOTS * SpDiag::nameLen;

    char* keyNames = reinterpret_cast<char*>(buffer.data() + hdr->keyNamesOffset);
    char* keyModuleNames = reinterpret_cast<char*>(buffer.data() + hdr->keyModuleNamesOffset);

    std::strncpy(keyNames + 3 * SpDiag::nameLen, "TestPoint", SpDiag::nameLen - 1);
    std::strncpy(keyModuleNames + 3 * SpDiag::nameLen, "TestModule", SpDiag::nameLen - 1);

    EXPECT_STREQ(keyNames + 3 * SpDiag::nameLen, "TestPoint");
    EXPECT_STREQ(keyModuleNames + 3 * SpDiag::nameLen, "TestModule");
}

TEST(ShmLayoutTest, ShardBufferResize) {
    SpDiag::ShardBuffer buf;
    EXPECT_TRUE(buf.empty());

    buf.resize(4, 16);
    EXPECT_EQ(buf.size(), 4u);
    EXPECT_EQ(buf.maxSlots(), 16u);
    EXPECT_FALSE(buf.empty());

    for (size_t i = 0; i < buf.size(); ++i) {
        SpDiag::PerfStat* stats = buf.stats(i);
        ASSERT_NE(stats, nullptr);
        stats[0].backGoodCount = 42;
        EXPECT_EQ(stats[0].backGoodCount, 42u);
    }
}

TEST(ShmLayoutTest, PerfStatReset) {
    SpDiag::PerfStat stat{};
    stat.backGoodCount = 10;
    stat.backBadCount = 5;
    stat.backNotCount = 3;
    stat.tickCount = 18;
    stat.totalTimeNs = 1000;
    stat.minTimeNs = 50;
    stat.maxTimeNs = 200;
    stat.slotIndex = 7;

    stat.reset();
    EXPECT_EQ(stat.backGoodCount, 0u);
    EXPECT_EQ(stat.backBadCount, 0u);
    EXPECT_EQ(stat.backNotCount, 0u);
    EXPECT_EQ(stat.tickCount, 0u);
    EXPECT_EQ(stat.totalTimeNs, 0u);
    EXPECT_EQ(stat.minTimeNs, 0u);
    EXPECT_EQ(stat.maxTimeNs, 0u);
    EXPECT_EQ(stat.slotIndex, 0u);
}
