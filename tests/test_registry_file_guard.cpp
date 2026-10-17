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
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cstdlib>
#include <cstdio>
#include <string>

#include "sdk/registry_file_guard.h"

namespace {

using namespace std::chrono_literals;

class TempFile {
public:
    TempFile() {
        std::array<char, 64> path{};
        std::snprintf(path.data(), path.size(),
                      "/tmp/spdiag_registry_lock_XXXXXX");
        fd_ = mkstemp(path.data());
        path_ = path.data();
    }

    ~TempFile() {
        if (fd_ >= 0) close(fd_);
        if (!path_.empty()) unlink(path_.c_str());
    }

    int Fd() const { return fd_; }
    const std::string& Path() const { return path_; }

private:
    int fd_ = -1;
    std::string path_;
};

TEST(RegistryFileGuardTest, CompetingDescriptionTimesOut) {
    TempFile file;
    ASSERT_GE(file.Fd(), 0);
    int secondFd = open(file.Path().c_str(), O_RDWR);
    ASSERT_GE(secondFd, 0);

    int errorCode = 0;
    auto first = SpDiag::RegistryFileGuard::AcquireFor(file.Fd(), 0ms,
                                                       &errorCode);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(errorCode, 0);

    const auto start = std::chrono::steady_clock::now();
    auto second = SpDiag::RegistryFileGuard::AcquireFor(secondFd, 20ms,
                                                        &errorCode);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_FALSE(second.has_value());
    EXPECT_EQ(errorCode, ETIMEDOUT);
    EXPECT_GE(elapsed, 10ms);
    EXPECT_LT(elapsed, 1s);

    first.reset();
    EXPECT_TRUE(SpDiag::RegistryFileGuard::AcquireFor(secondFd, 20ms));
    close(secondFd);
}

TEST(RegistryFileGuardTest, SigkillReleasesKernelLock) {
    TempFile file;
    ASSERT_GE(file.Fd(), 0);
    int ready[2];
    ASSERT_EQ(pipe(ready), 0);

    pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        close(ready[0]);
        int childFd = open(file.Path().c_str(), O_RDWR);
        auto guard = SpDiag::RegistryFileGuard::AcquireFor(childFd, 100ms);
        const char state = guard.has_value() ? '1' : '0';
        ssize_t written;
        do {
            written = write(ready[1], &state, 1);
        } while (written < 0 && errno == EINTR);
        if (written != 1) {
            _exit(4);
        }
        if (!guard) {
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
    ASSERT_EQ(kill(child, SIGKILL), 0);
    int status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFSIGNALED(status));
    ASSERT_EQ(WTERMSIG(status), SIGKILL);

    int parentFd = open(file.Path().c_str(), O_RDWR);
    ASSERT_GE(parentFd, 0);
    EXPECT_TRUE(SpDiag::RegistryFileGuard::AcquireFor(parentFd, 100ms));
    close(parentFd);
}

TEST(RegistryFileGuardTest, InvalidDescriptorFailsImmediately) {
    int errorCode = 0;
    EXPECT_FALSE(SpDiag::RegistryFileGuard::AcquireFor(-1, 100ms,
                                                       &errorCode));
    EXPECT_EQ(errorCode, EBADF);
}

}  // namespace
