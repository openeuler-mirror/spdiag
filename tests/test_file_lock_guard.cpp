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

#include <cerrno>
#include <chrono>
#include <csignal>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "manager/file_lock_guard.h"

namespace {

using namespace std::chrono_literals;

const std::string kLockName =
    SpDiag::MakeLockName("test", "file-lock-guard");

class FileLockGuardTest : public ::testing::Test {
protected:
    void SetUp() override { shm_unlink(kLockName.c_str()); }
    void TearDown() override { shm_unlink(kLockName.c_str()); }
};

}  // namespace

TEST_F(FileLockGuardTest, ContendingDescriptionTimesOut) {
    auto first = SpDiag::FileLockGuard::AcquireFor(kLockName, 0ms);
    ASSERT_TRUE(first.has_value());

    int errorCode = 0;
    auto second = SpDiag::FileLockGuard::AcquireFor(kLockName, 20ms,
                                                     &errorCode);
    EXPECT_FALSE(second.has_value());
    EXPECT_EQ(errorCode, ETIMEDOUT);

    first.reset();
    EXPECT_TRUE(SpDiag::FileLockGuard::AcquireFor(kLockName, 20ms));
}

TEST_F(FileLockGuardTest, SigkillReleasesKernelLock) {
    int ready[2];
    ASSERT_EQ(pipe(ready), 0);
    pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        close(ready[0]);
        auto guard = SpDiag::FileLockGuard::AcquireFor(kLockName, 100ms);
        const char state = guard.has_value() ? '1' : '0';
        ssize_t written;
        do {
            written = write(ready[1], &state, 1);
        } while (written < 0 && errno == EINTR);
        if (written != 1 || !guard) {
            _exit(2);
        }
        pause();
        _exit(3);
    }

    close(ready[1]);
    char state = '0';
    ASSERT_EQ(read(ready[0], &state, 1), 1);
    close(ready[0]);
    ASSERT_EQ(state, '1');
    EXPECT_FALSE(SpDiag::FileLockGuard::AcquireFor(kLockName, 0ms));

    ASSERT_EQ(kill(child, SIGKILL), 0);
    int status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFSIGNALED(status));
    EXPECT_TRUE(SpDiag::FileLockGuard::AcquireFor(kLockName, 100ms));
}
