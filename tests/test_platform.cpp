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
#include "sdk/platform.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>

namespace {
constexpr const char* kShmName = "/spdiag_test_platform_unit";
constexpr size_t kShmSize = 4096;
}  // namespace

TEST(PlatformTimestampTest, CalibrateTimestampReturnsPositive) {
    const uint64_t value = SpDiag::Platform::CalibrateTimestamp();
    EXPECT_GT(value, 0ULL);
}

TEST(PlatformTimestampTest, CalibrateTimestampIsStableAcrossCalls) {
    const uint64_t first  = SpDiag::Platform::CalibrateTimestamp();
    const uint64_t second = SpDiag::Platform::CalibrateTimestamp();
    EXPECT_GT(first, 0ULL);
    EXPECT_GT(second, 0ULL);
    // Loose order-of-magnitude check: two rapid calibrations of the same
    // hardware clock should not diverge by more than a small factor.
    const uint64_t hi = std::max(first, second);
    const uint64_t lo = std::min(first, second);
    ASSERT_NE(lo, 0ULL);
    EXPECT_LE(hi / lo, 10ULL);
}

TEST(PlatformTimestampTest, GetTimestampNsIsMonotonic) {
    // CalibrateTimestamp() is auto-invoked on first use, but call it
    // explicitly so every sample is taken within a single calibration epoch.
    SpDiag::Platform::CalibrateTimestamp();

    uint64_t prev = SpDiag::Platform::GetTimestampNs();
    for (int i = 0; i < 100; ++i) {
        const uint64_t curr = SpDiag::Platform::GetTimestampNs();
        EXPECT_GE(curr, prev);
        prev = curr;
    }
}

TEST(PlatformCpuTest, GetCurrentCpuIdIsPlausible) {
    const uint32_t cpu = SpDiag::Platform::GetCurrentCpuId();
    // Sanity bound: core IDs on any realistic machine are well under 1M.
    EXPECT_LT(cpu, 1u << 20);
}

TEST(PlatformShmTest, CreateCloseDestroyLifecycle) {
    // Clear any stale mapping from a prior run first; must not crash even
    // when the name does not exist (and is a documented no-op on Windows).
    SpDiag::Platform::ShmDestroy(kShmName);

    SpDiag::ShmHandle handle =
        SpDiag::Platform::ShmCreate(kShmName, kShmSize);
    EXPECT_NE(handle.baseAddr, nullptr);
    EXPECT_GE(handle.mappedSize, kShmSize);

    SpDiag::Platform::ShmClose(handle);
    EXPECT_EQ(handle.baseAddr, nullptr);

    SpDiag::Platform::ShmDestroy(kShmName);
    SUCCEED();
}

TEST(PlatformShmTest, OpenRejectsObjectSmallerThanRequestedMapping) {
    SpDiag::Platform::ShmDestroy(kShmName);
    SpDiag::ShmHandle created =
        SpDiag::Platform::ShmCreate(kShmName, sizeof(uint32_t));
    ASSERT_NE(created.baseAddr, nullptr);
    SpDiag::Platform::ShmClose(created);

    SpDiag::ShmHandle opened =
        SpDiag::Platform::ShmOpenReadOnly(kShmName, kShmSize);
    EXPECT_EQ(opened.baseAddr, nullptr);
    EXPECT_NE(opened.errorCode, 0);
    SpDiag::Platform::ShmDestroy(kShmName);
}

TEST(PlatformShmTest, ExclusiveCreateDoesNotTruncateExistingObject) {
    ASSERT_TRUE(SpDiag::Platform::ShmDestroy(kShmName));
    SpDiag::ShmHandle first =
        SpDiag::Platform::ShmCreate(kShmName, kShmSize);
    ASSERT_NE(first.baseAddr, nullptr);
    std::memset(first.baseAddr, 0x5a, kShmSize);

    SpDiag::ShmHandle second =
        SpDiag::Platform::ShmCreate(kShmName, kShmSize * 2);
    EXPECT_EQ(second.baseAddr, nullptr);
    EXPECT_EQ(static_cast<unsigned char*>(first.baseAddr)[0], 0x5a);

    SpDiag::Platform::ShmClose(first);
    EXPECT_TRUE(SpDiag::Platform::ShmDestroy(kShmName));
}

TEST(PlatformShmTest, CreatePublishesReadWriteModeIndependentOfUmask) {
    ASSERT_TRUE(SpDiag::Platform::ShmDestroy(kShmName));
    const mode_t oldMask = umask(0077);
    SpDiag::ShmHandle handle =
        SpDiag::Platform::ShmCreate(kShmName, kShmSize);
    umask(oldMask);

    ASSERT_NE(handle.baseAddr, nullptr);
    struct stat info {};
    ASSERT_EQ(fstat(handle.fd, &info), 0);
    EXPECT_EQ(info.st_mode & 0777, 0666);
    EXPECT_TRUE(SpDiag::Platform::ShmCheckReadWrite(kShmName));

    SpDiag::Platform::ShmClose(handle);
    EXPECT_TRUE(SpDiag::Platform::ShmDestroy(kShmName));
}

TEST(PlatformShmTest, InvalidNameReportsExistenceError) {
    int errorCode = 0;
    EXPECT_EQ(SpDiag::Platform::ShmCheckExistence("/invalid/name",
                                                   &errorCode),
              SpDiag::ShmExistence::ERROR);
    EXPECT_EQ(errorCode, EINVAL);
}
