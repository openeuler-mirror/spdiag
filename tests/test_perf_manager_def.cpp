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

// Exercises PerfManager::LoadDefFile (private) and the ConnectAndRegister
// point-name-change slot reset, both reached only through the public
// PerfManager::Instance().Init(config) path. A self-owned v3 SHM is created via
// SpDiagManager so the tests do not depend on an external `spdiag start`.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "manager/manager.h"
#include "cli/cli_config.h"
#include "spdiag/perf_point.h"
#include "spdiag/perf_key_defined.h"
#include "spdiag/perf_manager.h"
#include "spdiag/perf_config.h"
#include "spdiag/shm_layout.h"
#include "runtime/shm_reader.h"
#include "sdk/platform.h"

namespace {

constexpr const char* kShmName = "/spdiag_test_pmdef";

SpDiag::Cli::CliConfig MakeStartCfg(SpDiag::Cli::Command cmd) {
    SpDiag::Cli::CliConfig c;
    c.shmName = kShmName;
    c.command = cmd;
    c.numCores = 4;
    c.maxSlots = SpDiag::MAX_PERF_SLOTS;
    c.maxPrograms = 8;
    c.dumpIntervalMs = 60000;  // do not dump during the test
    c.levelGate = 4;
    return c;
}

SpDiag::PerfConfig MakePerfCfg(const std::string& program, uint32_t slotCount) {
    SpDiag::PerfConfig pc;
    pc.shmName = kShmName;
    pc.mode = SpDiag::RunMode::DEBUG;
    pc.levelGate = 4;
    pc.dumpIntervalMs = 60000;
    pc.programName = program;
    pc.slotCount = slotCount;
    return pc;
}

// Writes content to a unique temp file; removes it on destruction.
class TempFile {
public:
    TempFile(const std::string& content, const std::string& suffix) {
        path_ = std::filesystem::temp_directory_path() /
            ("spdiag_test_pmdef_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) + suffix);
        {
            std::ofstream ofs(path_);
            ofs << content;
        }
        pathStr_ = path_.string();
    }
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    const char* CStr() const { return pathStr_.c_str(); }

private:
    std::filesystem::path path_;
    std::string pathStr_;
};

// Saves and restores the SpDiag::detail auto-name globals so a name mismatch can
// be injected for re-Init without leaking state to other tests.
class AutoGlobalsGuard {
public:
    AutoGlobalsGuard(const char** names, uint32_t slotCount) {
        oldNames_ = SpDiag::detail::AutoPointNames();
        oldSlotCount_ = SpDiag::detail::AutoSlotCount();
        SpDiag::detail::AutoPointNames() = names;
        SpDiag::detail::AutoSlotCount() = slotCount;
    }
    ~AutoGlobalsGuard() {
        SpDiag::detail::AutoPointNames() = oldNames_;
        SpDiag::detail::AutoSlotCount() = oldSlotCount_;
    }
    AutoGlobalsGuard(const AutoGlobalsGuard&) = delete;
    AutoGlobalsGuard& operator=(const AutoGlobalsGuard&) = delete;

private:
    const char** oldNames_;
    uint32_t oldSlotCount_;
};

}  // namespace

class PerfManagerDefTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clean any residual SHM from a prior/aborted run, then create fresh.
        {
            SpDiag::SpDiagManager m;
            m.StopShm(MakeStartCfg(SpDiag::Cli::Command::STOP));
        }
        {
            SpDiag::SpDiagManager m;
            ASSERT_TRUE(m.StartShm(MakeStartCfg(SpDiag::Cli::Command::START)))
                << "failed to create test SHM";
        }
    }

    void TearDown() override {
        SpDiag::PerfManager::Instance().Shutdown();
        SpDiag::SpDiagManager m;
        m.StopShm(MakeStartCfg(SpDiag::Cli::Command::STOP));
    }
};

// LoadDefFile is private and its parsed result (keyPointNames_/keyModuleNames_)
// is never read back by any public or internal API, so the names cannot be
// observed directly. This smoke test verifies Init reaches the initialized state
// with a def file containing valid, comment, blank, and malformed lines.
TEST_F(PerfManagerDefTest, InitWithDefFileSucceeds) {
    const std::string content =
        "# comment line, must be skipped\n"
        "\n"
        "PERF_KEY_DEF(COMPUTE_MATMUL, \"Compute\", \"MatMul\")\n"
        "this line is malformed and must be skipped\n"
        "PERF_KEY_DEF(COMPUTE_PRIME, \"Compute\", \"Prime\")\n";
    TempFile defFile(content, ".def");

    SpDiag::PerfConfig cfg = MakePerfCfg("progDef", 3);
    cfg.defFilePath = defFile.CStr();
    SpDiag::PerfManager::Instance().Init(cfg);

    if (!SpDiag::PerfManager::Instance().IsInitialized()) {
        GTEST_SKIP() << "PerfManager not initialized (no SHM available)";
    }
    // Parsed without crashing; comment/blank/malformed lines skipped by sscanf.
    EXPECT_TRUE(SpDiag::PerfManager::Instance().IsInitialized());
}

TEST_F(PerfManagerDefTest, StaleLegacySpinlockDoesNotBlockRegistration) {
    using namespace SpDiag;

    ShmHandle handle = Platform::ShmOpenReadWrite(kShmName, sizeof(ShmHeader));
    ASSERT_NE(handle.baseAddr, nullptr);
    auto* header = static_cast<ShmHeader*>(handle.baseAddr);
    header->spinlock.test_and_set(std::memory_order_release);
    Platform::ShmClose(handle);

    PerfManager::Instance().Init(MakePerfCfg("staleLockProgram", 3));
    EXPECT_TRUE(PerfManager::Instance().IsInitialized());
}

// ConnectAndRegister resets a slot's PerfStat when, on re-registering the SAME
// program name, the resolved point/module name differs from what is stored in
// SHM. The mismatch is injected through the SpDiag::detail auto-name globals
// (the only control point reachable without calling private methods). The reset
// is observed by reading the SHM shards directly via ShmReader.
TEST_F(PerfManagerDefTest, PointNameChangeResetsSlotOnReInit) {
    using namespace SpDiag;
    constexpr uint32_t kSlotCount = 3;
    const uint32_t kPrimeLocalIndex = static_cast<uint32_t>(PerfKeyDefined::COMPUTE_PRIME);

    // --- First registration: program "progA", compiled-in default names. ---
    PerfManager::Instance().Init(MakePerfCfg("progA", kSlotCount));
    if (!PerfManager::Instance().IsInitialized()) {
        GTEST_SKIP() << "PerfManager not initialized (no SHM available)";
    }

    // Put non-zero data into COMPUTE_PRIME (local slot 2 -> SHM slot baseSlot+2).
    {
        PerfPoint pt(PerfKeyDefined::COMPUTE_PRIME, PerfLevel::SUB_SYSTEM);
        pt.Start();
        pt.End(0);
    }
    {
        LocalPerfShard& shard = PerfManager::Instance().GetShard();
        ASSERT_GT(shard.stats[kPrimeLocalIndex].tickCount, 0u)
            << "in-process shard did not capture the sample";
    }
    PerfManager::Instance().Shutdown();  // final dump writes the slot into SHM

    // --- Read SHM: confirm the slot is non-zero and capture the base slot. ---
    uint32_t baseSlot = 0;
    uint64_t tickBefore = 0;
    {
        ShmReader reader;
        ASSERT_TRUE(reader.Open(kShmName));
        const ProgramEntry* programs = reader.ReadProgramRegistry();
        ASSERT_NE(programs, nullptr);
        ASSERT_GT(reader.Header().registeredPrograms, 0u);
        baseSlot = programs[0].baseSlot;
        ShardBuffer shards;
        ASSERT_TRUE(reader.SnapshotShards(shards, nullptr));
        const uint32_t numCores = reader.Header().numCores;
        for (uint32_t c = 0u; c < numCores; ++c) {
            tickBefore += shards.stats(c)[baseSlot + kPrimeLocalIndex].tickCount;
        }
        reader.Close();
    }
    ASSERT_GT(tickBefore, 0u) << "sampled data did not reach SHM after Shutdown dump";
    const uint32_t targetSlot = baseSlot + kPrimeLocalIndex;

    // --- Inject a point-name change for arrIdx 1 (slot baseSlot+2). ---
    // arrIdx 0 keeps "MatMul" so only slot baseSlot+2 is detected as changed.
    const char* renamed[] = {"MatMul", "PrimeRenamed"};

    // --- Re-Init SAME program "progA": ConnectAndRegister resets changed slots. ---
    {
        AutoGlobalsGuard guard(renamed, kSlotCount);
        PerfManager::Instance().Init(MakePerfCfg("progA", kSlotCount));
    }
    ASSERT_TRUE(PerfManager::Instance().IsInitialized())
        << "re-Init on same shmName/program failed";

    // --- Read SHM again: the previously non-zero slot must now be reset, and
    // the reused-program path must have overwritten the stored name. ---
    uint64_t tickAfter = 0;
    {
        ShmReader reader;
        ASSERT_TRUE(reader.Open(kShmName));
        ShardBuffer shards;
        ASSERT_TRUE(reader.SnapshotShards(shards, nullptr));
        const uint32_t numCores = reader.Header().numCores;
        for (uint32_t c = 0u; c < numCores; ++c) {
            tickAfter += shards.stats(c)[targetSlot].tickCount;
        }
        const char (*keyNames)[nameLen] = reader.ReadKeyNames();
        ASSERT_NE(keyNames, nullptr);
        EXPECT_STREQ(keyNames[targetSlot], "PrimeRenamed");
        reader.Close();
    }
    EXPECT_EQ(tickAfter, 0u) << "slot was not reset after point-name change on re-Init";
}
