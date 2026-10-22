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
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cli/cli_config.h"
#include "manager/file_lock_guard.h"
#include "manager/manager.h"
#include "sdk/platform.h"
#include "sdk/shm_probe.h"
#include "sdk/shm_writer.h"

namespace {

constexpr const char* kLifecycleShm = "/spdiag_test_lifecycle";

SpDiag::Cli::CliConfig MakeConfig() {
    SpDiag::Cli::CliConfig config;
    config.shmName = kLifecycleShm;
    config.dataDir = "/tmp/spdiag_test_lifecycle";
    config.numCores = 2;
    config.maxSlots = 64;
    config.maxPrograms = 4;
    return config;
}

class ChildMapping {
public:
    ~ChildMapping() { Stop(); }

    bool Start(const std::string& name, size_t size) {
        int ready[2];
        if (pipe(ready) != 0) {
            return false;
        }
        pid_ = fork();
        if (pid_ < 0) {
            close(ready[0]);
            close(ready[1]);
            return false;
        }
        if (pid_ == 0) {
            close(ready[0]);
            SpDiag::ShmHandle mapped =
                SpDiag::Platform::ShmOpenReadWrite(name, size);
            const char state = mapped.baseAddr != nullptr ? '1' : '0';
            ssize_t written;
            do {
                written = write(ready[1], &state, 1);
            } while (written < 0 && errno == EINTR);
            if (written != 1 || mapped.baseAddr == nullptr) {
                _exit(2);
            }
            pause();
            _exit(3);
        }

        close(ready[1]);
        char state = '0';
        const ssize_t count = read(ready[0], &state, 1);
        close(ready[0]);
        if (count != 1 || state != '1') {
            Stop();
            return false;
        }
        return true;
    }

    void Stop() {
        if (pid_ <= 0) {
            return;
        }
        (void)kill(pid_, SIGKILL);
        int status = 0;
        while (waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
        }
        pid_ = -1;
    }

    bool IsAlive() const {
        return pid_ > 0 && kill(pid_, 0) == 0;
    }

private:
    pid_t pid_ = -1;
};

class ShmLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override {
        SpDiag::Platform::ShmDestroy(kLifecycleShm);
    }

    void TearDown() override {
        SpDiag::Platform::ShmDestroy(kLifecycleShm);
        const auto config = MakeConfig();
        shm_unlink(SpDiag::MakeLockName("lifecycle", config.shmName).c_str());
    }
};

int ExerciseOwnerModeZeroRecovery(bool restart) {
    SpDiag::SpDiagManager manager;
    const auto config = MakeConfig();
    if (!manager.StartShm(config)) {
        return 10;
    }
    const int fd = shm_open(config.shmName.c_str(), O_RDWR | O_CLOEXEC, 0);
    if (fd < 0) {
        return 11;
    }
    const int chmodResult = fchmod(fd, 0000);
    close(fd);
    if (chmodResult != 0) {
        return 12;
    }

    if (restart) {
        if (!manager.RestartShm(config) ||
            !SpDiag::ProbeShmHeader(config.shmName).valid) {
            return 13;
        }
        return manager.StopShm(config) ? 0 : 14;
    }
    if (!manager.StopShm(config)) {
        return 15;
    }
    return SpDiag::Platform::ShmCheckExistence(config.shmName) ==
                   SpDiag::ShmExistence::ABSENT
               ? 0
               : 16;
}

void ExpectOwnerModeZeroRecovery(bool restart) {
    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        if (geteuid() == 0 && setuid(65534) != 0) {
            _exit(20);
        }
        _exit(ExerciseOwnerModeZeroRecovery(restart));
    }
    int status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

}  // namespace

TEST_F(ShmLifecycleTest, StartOnCompatibleMappedShmIsIdempotent) {
    SpDiag::SpDiagManager manager;
    ASSERT_TRUE(manager.StartShm(MakeConfig()));
    const auto probe = SpDiag::ProbeShmHeader(kLifecycleShm);
    ASSERT_TRUE(probe.valid);
    ChildMapping child;
    ASSERT_TRUE(child.Start(kLifecycleShm, probe.totalSize));

    EXPECT_TRUE(manager.StartShm(MakeConfig()));
    EXPECT_TRUE(SpDiag::ProbeShmHeader(kLifecycleShm).valid);
}

TEST_F(ShmLifecycleTest, StartRejectsIncompatibleShmWithoutMutation) {
    SpDiag::SpDiagManager manager;
    ASSERT_TRUE(manager.StartShm(MakeConfig()));
    const auto probe = SpDiag::ProbeShmHeader(kLifecycleShm);
    ASSERT_TRUE(probe.valid);

    SpDiag::ShmHandle handle =
        SpDiag::Platform::ShmOpenReadWrite(kLifecycleShm, probe.totalSize);
    ASSERT_NE(handle.baseAddr, nullptr);
    auto* header = reinterpret_cast<SpDiag::ShmHeader*>(handle.baseAddr);
    header->magic = 0;
    header->lastDumpTimestampNs = 0x12345678ULL;
    SpDiag::Platform::ShmClose(handle);

    EXPECT_FALSE(manager.StartShm(MakeConfig()));
    EXPECT_EQ(SpDiag::Platform::ShmCheckExistence(kLifecycleShm),
              SpDiag::ShmExistence::PRESENT);

    handle = SpDiag::Platform::ShmOpenReadOnly(kLifecycleShm, probe.totalSize);
    ASSERT_NE(handle.baseAddr, nullptr);
    const auto* unchanged =
        reinterpret_cast<const SpDiag::ShmHeader*>(handle.baseAddr);
    EXPECT_EQ(unchanged->magic, 0u);
    EXPECT_EQ(unchanged->lastDumpTimestampNs, 0x12345678ULL);
    SpDiag::Platform::ShmClose(handle);
}

TEST_F(ShmLifecycleTest, RestartRepairsCorruptedShmAfterBusinessStops) {
    SpDiag::SpDiagManager manager;
    ASSERT_TRUE(manager.StartShm(MakeConfig()));
    const auto probe = SpDiag::ProbeShmHeader(kLifecycleShm);
    ASSERT_TRUE(probe.valid);

    SpDiag::ShmHandle oldHandle =
        SpDiag::Platform::ShmOpenReadWrite(kLifecycleShm, probe.totalSize);
    ASSERT_NE(oldHandle.baseAddr, nullptr);
    auto* oldHeader = reinterpret_cast<SpDiag::ShmHeader*>(oldHandle.baseAddr);
    oldHeader->magic = 0;
    oldHeader->lastDumpTimestampNs = 0x12345678ULL;
    SpDiag::Platform::ShmClose(oldHandle);
    EXPECT_FALSE(SpDiag::ProbeShmHeader(kLifecycleShm).valid);

    EXPECT_TRUE(manager.RestartShm(MakeConfig()));
    const auto repaired = SpDiag::ProbeShmHeader(kLifecycleShm);
    ASSERT_TRUE(repaired.valid);
    SpDiag::ShmHandle newHandle =
        SpDiag::Platform::ShmOpenReadOnly(kLifecycleShm, repaired.totalSize);
    ASSERT_NE(newHandle.baseAddr, nullptr);
    const auto* newHeader =
        reinterpret_cast<const SpDiag::ShmHeader*>(newHandle.baseAddr);
    EXPECT_EQ(newHeader->magic, SpDiag::shmMagic);
    EXPECT_EQ(newHeader->lastDumpTimestampNs, 0u);
    SpDiag::Platform::ShmClose(newHandle);
}

TEST_F(ShmLifecycleTest, StopUnlinksWithoutMutatingExistingMapping) {
    SpDiag::SpDiagManager manager;
    ASSERT_TRUE(manager.StartShm(MakeConfig()));
    const auto probe = SpDiag::ProbeShmHeader(kLifecycleShm);
    ASSERT_TRUE(probe.valid);
    SpDiag::ShmWriter writer;
    ASSERT_TRUE(writer.Open(kLifecycleShm, probe.totalSize));
    writer.Header()->lastDumpTimestampNs = 0x12345678ULL;
    ChildMapping child;
    ASSERT_TRUE(child.Start(kLifecycleShm, probe.totalSize));

    EXPECT_TRUE(manager.StopShm(MakeConfig()));
    EXPECT_EQ(writer.Header()->lastDumpTimestampNs, 0x12345678ULL);
    EXPECT_FALSE(SpDiag::ProbeShmHeader(kLifecycleShm).valid);
    writer.Release();
}

TEST_F(ShmLifecycleTest, StopBusinessThenStartCreatesFreshShm) {
    SpDiag::SpDiagManager manager;
    ASSERT_TRUE(manager.StartShm(MakeConfig()));
    const auto probe = SpDiag::ProbeShmHeader(kLifecycleShm);
    ASSERT_TRUE(probe.valid);

    SpDiag::ShmHandle oldHandle =
        SpDiag::Platform::ShmOpenReadWrite(kLifecycleShm, probe.totalSize);
    ASSERT_NE(oldHandle.baseAddr, nullptr);
    reinterpret_cast<SpDiag::ShmHeader*>(oldHandle.baseAddr)
        ->lastDumpTimestampNs = 0xabcdefULL;
    SpDiag::Platform::ShmClose(oldHandle);

    ChildMapping child;
    ASSERT_TRUE(child.Start(kLifecycleShm, probe.totalSize));
    ASSERT_TRUE(manager.StopShm(MakeConfig()));
    EXPECT_TRUE(child.IsAlive());
    EXPECT_EQ(SpDiag::Platform::ShmCheckExistence(kLifecycleShm),
              SpDiag::ShmExistence::ABSENT);

    child.Stop();
    ASSERT_TRUE(manager.StartShm(MakeConfig()));

    const auto newProbe = SpDiag::ProbeShmHeader(kLifecycleShm);
    ASSERT_TRUE(newProbe.valid);
    SpDiag::ShmHandle newHandle =
        SpDiag::Platform::ShmOpenReadOnly(kLifecycleShm, newProbe.totalSize);
    ASSERT_NE(newHandle.baseAddr, nullptr);
    const auto* newHeader =
        reinterpret_cast<const SpDiag::ShmHeader*>(newHandle.baseAddr);
    EXPECT_EQ(newHeader->lastDumpTimestampNs, 0u);
    SpDiag::Platform::ShmClose(newHandle);
}

TEST_F(ShmLifecycleTest, StartRejectsInvalidShmNameInsteadOfReportingRunning) {
    auto config = MakeConfig();
    config.shmName = "/invalid/name";
    SpDiag::SpDiagManager manager;
    EXPECT_FALSE(manager.StartShm(config));
}

TEST_F(ShmLifecycleTest, OwnerCanStopModeZeroShm) {
    ExpectOwnerModeZeroRecovery(false);
}

TEST_F(ShmLifecycleTest, OwnerCanRestartModeZeroShm) {
    ExpectOwnerModeZeroRecovery(true);
}

TEST_F(ShmLifecycleTest, NonOwnerCannotStopSharedShm) {
    if (geteuid() != 0) {
        GTEST_SKIP() << "setuid is required for the non-owner control case";
    }
    SpDiag::SpDiagManager manager;
    ASSERT_TRUE(manager.StartShm(MakeConfig()));

    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        if (setuid(65534) != 0) {
            _exit(20);
        }
        SpDiag::SpDiagManager childManager;
        _exit(childManager.StopShm(MakeConfig()) ? 21 : 0);
    }
    int status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
    EXPECT_EQ(SpDiag::Platform::ShmCheckExistence(kLifecycleShm),
              SpDiag::ShmExistence::PRESENT);
}
