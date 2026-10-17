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

#include "cli/cli_config.h"
#include "manager/manager.h"
#include "runtime/shm_reader.h"
#include "sdk/platform.h"
#include "sdk/shm_probe.h"
#include "sdk/shm_writer.h"

namespace {

constexpr const char* kProbeShm = "/spdiag_test_probe_unit";
constexpr uint32_t kLegacyShmMagic = 0x50455246u;

SpDiag::Cli::CliConfig MakeConfig() {
    SpDiag::Cli::CliConfig config;
    config.shmName = kProbeShm;
    config.dataDir = "/tmp/spdiag_test_probe_unit";
    config.numCores = 2;
    config.maxSlots = 64;
    config.maxPrograms = 4;
    return config;
}

class ShmProbeTest : public ::testing::Test {
protected:
    void SetUp() override {
        SpDiag::Platform::ShmDestroy(kProbeShm);
    }

    void TearDown() override {
        SpDiag::Platform::ShmDestroy(kProbeShm);
    }
};

}  // namespace

TEST_F(ShmProbeTest, AcceptsMatchingAbi) {
    SpDiag::SpDiagManager manager;
    ASSERT_TRUE(manager.StartShm(MakeConfig()));
    auto probe = SpDiag::ProbeShmHeader(kProbeShm);
    EXPECT_TRUE(probe.IsCompatible());
    EXPECT_GT(probe.totalSize, sizeof(SpDiag::ShmHeader));
}

TEST_F(ShmProbeTest, LegacyAndCurrentProbesRejectEachOthersMagic) {
    SpDiag::SpDiagManager manager;
    ASSERT_TRUE(manager.StartShm(MakeConfig()));
    auto compatible = SpDiag::ProbeShmHeader(kProbeShm);
    ASSERT_TRUE(compatible.IsCompatible());

    SpDiag::ShmWriter writer;
    ASSERT_TRUE(writer.Open(kProbeShm, compatible.totalSize));
    const SpDiag::ShmHeader* currentHeader = writer.Header();
    const bool legacyWouldAccept =
        currentHeader->magic == kLegacyShmMagic &&
        currentHeader->shmTotalSize > 0;
    EXPECT_FALSE(legacyWouldAccept);

    writer.Header()->magic = kLegacyShmMagic;
    writer.Release();
    EXPECT_EQ(SpDiag::ProbeShmHeader(kProbeShm).status,
              SpDiag::ShmProbeStatus::MALFORMED);
}

TEST_F(ShmProbeTest, RejectsFeatureMismatchAcrossProbeAndReader) {
    SpDiag::SpDiagManager manager;
    ASSERT_TRUE(manager.StartShm(MakeConfig()));
    auto compatible = SpDiag::ProbeShmHeader(kProbeShm);
    ASSERT_TRUE(compatible.IsCompatible());

    SpDiag::ShmWriter writer;
    ASSERT_TRUE(writer.Open(kProbeShm, compatible.totalSize));
    writer.Header()->reserved[1] ^= SpDiag::shmFeaturePerfLog;
    writer.Release();

    auto incompatible = SpDiag::ProbeShmHeader(kProbeShm);
    EXPECT_EQ(incompatible.status, SpDiag::ShmProbeStatus::INCOMPATIBLE);
    SpDiag::ShmReader reader;
    EXPECT_FALSE(reader.Open(kProbeShm));
}

TEST_F(ShmProbeTest, RejectsTruncatedObjectWithoutMappingHeader) {
    SpDiag::ShmHandle handle =
        SpDiag::Platform::ShmCreate(kProbeShm, sizeof(uint32_t));
    ASSERT_NE(handle.baseAddr, nullptr);
    SpDiag::Platform::ShmClose(handle);

    auto result = SpDiag::ProbeShmHeader(kProbeShm);
    EXPECT_EQ(result.status, SpDiag::ShmProbeStatus::MALFORMED);
    EXPECT_FALSE(result.IsCompatible());
}

TEST_F(ShmProbeTest, RejectsOffsetMismatch) {
    SpDiag::SpDiagManager manager;
    ASSERT_TRUE(manager.StartShm(MakeConfig()));
    auto compatible = SpDiag::ProbeShmHeader(kProbeShm);
    ASSERT_TRUE(compatible.IsCompatible());

    SpDiag::ShmWriter writer;
    ASSERT_TRUE(writer.Open(kProbeShm, compatible.totalSize));
    writer.Header()->shardsOffset++;
    writer.Release();

    auto result = SpDiag::ProbeShmHeader(kProbeShm);
    EXPECT_EQ(result.status, SpDiag::ShmProbeStatus::INCOMPATIBLE);
}

TEST_F(ShmProbeTest, RejectsReplacementLargerThanMappedRange) {
    SpDiag::SpDiagManager manager;
    auto originalConfig = MakeConfig();
    ASSERT_TRUE(manager.StartShm(originalConfig));
    const auto original = SpDiag::ProbeShmHeader(kProbeShm);
    ASSERT_TRUE(original.IsCompatible());

    SpDiag::Platform::ShmDestroy(kProbeShm);
    auto replacementConfig = MakeConfig();
    replacementConfig.maxSlots = originalConfig.maxSlots * 2;
    ASSERT_TRUE(manager.StartShm(replacementConfig));
    const auto replacement = SpDiag::ProbeShmHeader(kProbeShm);
    ASSERT_TRUE(replacement.IsCompatible());
    ASSERT_GT(replacement.totalSize, original.totalSize);

    SpDiag::ShmHandle staleSizedMapping =
        SpDiag::Platform::ShmOpenReadOnly(kProbeShm, original.totalSize);
    ASSERT_NE(staleSizedMapping.baseAddr, nullptr);
    ASSERT_EQ(staleSizedMapping.mappedSize, original.totalSize);
    ASSERT_EQ(staleSizedMapping.objectSize, replacement.totalSize);

    const auto* header = static_cast<const SpDiag::ShmHeader*>(
        staleSizedMapping.baseAddr);
    const auto result =
        SpDiag::ValidateMappedShmHeader(*header, staleSizedMapping);
    EXPECT_EQ(result.status, SpDiag::ShmProbeStatus::INCOMPATIBLE);
    EXPECT_EQ(result.detail, "mapping does not cover the complete SHM object");
    SpDiag::Platform::ShmClose(staleSizedMapping);
}
