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

// ShmReader 集成测试。
// 自建 v3 SHM（经 SpDiagManager + PerfManager），本地点经 Shutdown 末次 dump 落入 SHM，
// 再以只读 ShmReader 验证 SnapshotShards / 注册表 / 名称表 / 全局区契约。
// PerfManager 未初始化（如 Windows 无 SHM）时各用例 GTEST_SKIP。
#include <gtest/gtest.h>
#include <atomic>
#include <cstdint>
#include <vector>
#include "manager/manager.h"
#include "cli/cli_config.h"
#include "spdiag/perf_point.h"
#include "spdiag/perf_key_defined.h"
#include "spdiag/perf_manager.h"
#include "spdiag/perf_config.h"
#include "spdiag/perf_level.h"
#include "spdiag/shm_layout.h"
#include "runtime/shm_reader.h"

namespace {

constexpr const char* kShmName = "/spdiag_test_shm_reader_unit";

SpDiag::Cli::CliConfig MakeStartCfg(const char* name) {
    SpDiag::Cli::CliConfig c;
    c.shmName = name;
    c.numCores = 4;
    c.maxSlots = SpDiag::MAX_PERF_SLOTS;   // 256，与 SDK 数组一致
    c.maxPrograms = 8;
    c.dumpIntervalMs = 60000;              // 测试期不主动 dump；数据由 Shutdown 末次 dump 落盘
    c.levelGate = 4;
    return c;
}

void BusyWork(int n = 5000) {
    for (volatile int i = 0; i < n; ++i) {
    }
}

// 对给定 ShardBuffer 的指定槽位跨核累加某字段（本地点仅落在调用线程所在核，故需遍历核）。
template <typename Field>
uint64_t SumAcrossCores(const SpDiag::ShardBuffer& s, uint32_t slot, Field SpDiag::PerfStat::* field) {
    uint64_t total = 0;
    for (size_t c = 0; c < s.size(); ++c) {
        total += static_cast<uint64_t>(s.stats(c)[slot].*field);
    }
    return total;
}

}  // namespace

class ShmReaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        using namespace SpDiag;
        // 清理残留 SHM，确保干净起点。
        {
            Cli::CliConfig c = MakeStartCfg(kShmName);
            c.command = Cli::Command::STOP;
            SpDiagManager m;
            m.StopShm(c);
        }
        {
            Cli::CliConfig c = MakeStartCfg(kShmName);
            c.command = Cli::Command::START;
            SpDiagManager m;
            m.StartShm(c);   // 失败则后续 Init 失败，各用例 GTEST_SKIP
        }

        PerfConfig pcfg;
        pcfg.shmName = kShmName;
        pcfg.mode = RunMode::DEBUG;
        pcfg.levelGate = 4;
        pcfg.dumpIntervalMs = 60000;
        PerfManager::Instance().Init(pcfg);
    }

    void TearDown() override {
        using namespace SpDiag;
        PerfManager::Instance().Shutdown();
        Cli::CliConfig c = MakeStartCfg(kShmName);
        c.command = Cli::Command::STOP;
        SpDiagManager m;
        m.StopShm(c);
    }
};

// 用例1：稳态（Shutdown 末次 dump 完成后 dumpSeqNo 回偶、无 writer 活动）下
// SnapshotShards 成功返回 true，且快照分片数据反映了本地点累计的 good/bad/tick 计数。
TEST_F(ShmReaderTest, SnapshotShardsReflectsAccumulatedCounts) {
    using namespace SpDiag;
    if (!PerfManager::Instance().IsInitialized()) {
        GTEST_SKIP() << "PerfManager not initialized (SHM unavailable on this platform)";
    }
    const auto key = PerfKeyDefined::COMPUTE_MATMUL;
    const uint32_t slot = PerfManager::Instance().FastResolveSlot(static_cast<uint32_t>(key));
    {
        PerfPoint good(key, PerfLevel::SUB_SYSTEM);
        good.Start();
        BusyWork();
        good.End(0);          // returnCode=0 → goodCount
    }
    {
        PerfPoint bad(key, PerfLevel::SUB_SYSTEM);
        bad.Start();
        BusyWork();
        bad.End(1);           // returnCode!=0 → badCount
    }
    PerfManager::Instance().Shutdown();   // 末次 dump：本地 shard 合并入 SHM

    ShmReader reader;
    ASSERT_TRUE(reader.Open(kShmName)) << "ShmReader::Open failed";
    const ShmHeader& hdr = reader.Header();
    EXPECT_EQ(hdr.magic, shmMagic);
    EXPECT_EQ(hdr.version, 3u);

    ShardBuffer shards;
    ASSERT_TRUE(reader.SnapshotShards(shards));

    EXPECT_FALSE(shards.empty());
    EXPECT_EQ(shards.size(), static_cast<size_t>(hdr.numCores));
    EXPECT_EQ(shards.maxSlots(), hdr.maxSlots);
    ASSERT_LT(slot, shards.maxSlots());

    const uint64_t tickTotal = SumAcrossCores(shards, slot, &PerfStat::tickCount);
    const uint64_t goodTotal = SumAcrossCores(shards, slot, &PerfStat::backGoodCount);
    const uint64_t badTotal  = SumAcrossCores(shards, slot, &PerfStat::backBadCount);
    EXPECT_GT(tickTotal, 0u);
    EXPECT_GT(goodTotal, 0u);
    EXPECT_GT(badTotal, 0u);

    // 末次 dump 完成后 dumpSeqNo 回到偶数（空闲），无 writer 活动。
    const uint64_t seq = hdr.dumpSeqNo.load();
    EXPECT_EQ(seq % 2ULL, 0ULL);
    reader.Close();
}

// 用例2：ReadProgramRegistry / ReadKeyNames / ReadKeyModuleNames 均可读且非空；
// ReadGlobalAccum 严格遵循头契约——globalAccumCount==0 时返回 nullptr，否则非空。
TEST_F(ShmReaderTest, RegistryNamesAndGlobalAccumContract) {
    using namespace SpDiag;
    if (!PerfManager::Instance().IsInitialized()) {
        GTEST_SKIP() << "PerfManager not initialized (SHM unavailable on this platform)";
    }
    {
        PerfPoint pt(PerfKeyDefined::COMPUTE_PRIME, PerfLevel::SUB_SYSTEM);
        pt.Start();
        BusyWork();
        pt.End(0);
    }
    PerfManager::Instance().Shutdown();

    ShmReader reader;
    ASSERT_TRUE(reader.Open(kShmName));
    const ShmHeader& hdr = reader.Header();

    const ProgramEntry* programs = reader.ReadProgramRegistry();
    ASSERT_NE(programs, nullptr);
    EXPECT_GE(hdr.registeredPrograms, 1u);
    EXPECT_TRUE(programs[0].active);
    EXPECT_GE(programs[0].slotCount, 1u);

    const char (*keyNames)[nameLen] = reader.ReadKeyNames();
    EXPECT_NE(keyNames, nullptr);

    const char (*keyModNames)[nameLen] = reader.ReadKeyModuleNames();
    EXPECT_NE(keyModNames, nullptr);

    // 头契约（shm_reader.h）：globalAccumCount==0 → nullptr；否则非空。
    const PerfStat* globalAccum = reader.ReadGlobalAccum();
    if (hdr.globalAccumCount == 0u) {
        EXPECT_EQ(globalAccum, nullptr);
    } else {
        EXPECT_NE(globalAccum, nullptr);
    }
    reader.Close();
}

// 用例3：奇偶重试回退。无法不经私有接口强制 dumpSeqNo 为奇——唯一 writer 是 PerfTimer，
// dumpSeqNo 仅在 DoOneDump 内经 CAS/fetch_add 变更，PerfManager 未暴露强制 dump 入口。
// 故断言正向一致性：在静止 SHM 上两次快速 SnapshotShards 均成功，且被测 key 的 tickCount
// 完全相等（无并发 writer，确定性结论）。同时验证 globalOut 出参的尺寸契约。
TEST_F(ShmReaderTest, RapidSnapshotsAreConsistent) {
    using namespace SpDiag;
    if (!PerfManager::Instance().IsInitialized()) {
        GTEST_SKIP() << "PerfManager not initialized (SHM unavailable on this platform)";
    }
    const auto key = PerfKeyDefined::COMPUTE_MATMUL;
    const uint32_t slot = PerfManager::Instance().FastResolveSlot(static_cast<uint32_t>(key));
    {
        PerfPoint pt(key, PerfLevel::SUB_SYSTEM);
        for (int i = 0; i < 5; ++i) {
            pt.Start();
            BusyWork();
            pt.End(0);
        }
    }
    PerfManager::Instance().Shutdown();   // 静止 SHM，无并发 writer

    ShmReader reader;
    ASSERT_TRUE(reader.Open(kShmName));

    ShardBuffer snap1;
    ShardBuffer snap2;
    ASSERT_TRUE(reader.SnapshotShards(snap1));
    ASSERT_TRUE(reader.SnapshotShards(snap2));

    EXPECT_EQ(snap1.size(), snap2.size());
    ASSERT_LT(slot, snap1.maxSlots());

    const uint64_t tick1 = SumAcrossCores(snap1, slot, &PerfStat::tickCount);
    const uint64_t tick2 = SumAcrossCores(snap2, slot, &PerfStat::tickCount);
    EXPECT_EQ(tick1, tick2);
    EXPECT_GT(tick1, 0u);

    // globalOut 出参：globalAccumCount>0 时按契约填充 globalAccumCount*sizeof(PerfStat) 字节。
    std::vector<uint8_t> globalData;
    ShardBuffer snap3;
    ASSERT_TRUE(reader.SnapshotShards(snap3, &globalData));
    const ShmHeader& hdr = reader.Header();
    if (hdr.globalAccumCount > 0u) {
        EXPECT_EQ(globalData.size(),
                  static_cast<size_t>(hdr.globalAccumCount) * sizeof(PerfStat));
    }
    reader.Close();
}
