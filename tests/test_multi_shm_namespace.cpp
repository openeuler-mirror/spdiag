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

#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <utility>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cli/cli_config.h"
#include "manager/manager.h"
#include "runtime/shm_reader.h"
#include "sdk/platform.h"
#include "spdiag/shm_layout.h"
#include "spdiag/perf_config.h"
#include "spdiag/perf_key_defined.h"
#include "spdiag/perf_manager.h"
#include "spdiag/perf_point.h"
#include "spdiag/shm_path.h"

namespace {

SpDiag::Cli::CliConfig MakeConfig(const std::string& name) {
    SpDiag::Cli::CliConfig config;
    config.shmName = name;
    config.numCores = 2;
    config.maxSlots = 512;
    config.maxPrograms = 2;
    return config;
}

int RunProducer(const std::string& path, const char* programName) {
    setenv(SpDiag::shmPathEnvironment, path.c_str(), 1);
    SpDiag::PerfConfig config;
    config.programName = programName;
    config.levelGate = 4;
    SpDiag::PerfManager::Instance().Init(config);
    if (!SpDiag::PerfManager::Instance().IsInitialized()) return 11;
    SpDiag::PerfPoint point(SpDiag::PerfKeyDefined::COMPUTE_MATMUL,
                            SpDiag::PerfLevel::KEY_MODULE);
    point.Start();
    point.End(0);
    SpDiag::PerfManager::Instance().Shutdown();
    return 0;
}

int RunLateAttachProducer(const std::string& path, int readyFd) {
    setenv(SpDiag::shmPathEnvironment, path.c_str(), 1);
    SpDiag::PerfPoint before(SpDiag::PerfKeyDefined::COMPUTE_MATMUL,
                             SpDiag::PerfLevel::KEY_MODULE);
    usleep(200000);
    char signal = 'R';
    if (write(readyFd, &signal, 1) != 1) return 21;
    for (int retry = 0; retry < 40; ++retry) {
        SpDiag::PerfPoint point(SpDiag::PerfKeyDefined::COMPUTE_MATMUL,
                                SpDiag::PerfLevel::KEY_MODULE);
        if (SpDiag::PerfManager::Instance().IsInitialized()) {
            point.Start();
            point.End(0);
            std::string shmName;
            std::string error;
            if (!SpDiag::NormalizeShmPath(path, shmName, error)) return 23;
            SpDiag::SpDiagManager manager;
            if (!manager.RequestFreshDump(MakeConfig(shmName))) return 24;
            usleep(200000);
            SpDiag::PerfManager::Instance().EnsureInitialized();
            if (!SpDiag::PerfManager::Instance().IsInitialized()) return 25;
            SpDiag::PerfManager::Instance().Shutdown();
            return 0;
        }
        usleep(100000);
    }
    return 22;
}

bool PublishDelayedShm(const SpDiag::Cli::CliConfig& config) {
    using namespace SpDiag;
    ShmLayoutDescriptor layout;
    if (!ComputeShmLayout(config.maxSlots, config.maxPrograms,
                          config.numCores, layout, config.perCoreTopN)) return false;
    const int fd = shm_open(config.shmName.c_str(),
                            O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) return false;
    if (ftruncate(fd, layout.totalSize) != 0) {
        close(fd);
        return false;
    }
    usleep(1500000);
    ShmHeader header{};
    header.magic = shmMagic;
    header.version = 3;
    header.levelGate = 4;
    header.reserved[0] = shmLayoutAbiRevision;
    header.reserved[1] = CurrentShmFeatureMask();
    header.maxSlots = config.maxSlots;
    header.maxPrograms = config.maxPrograms;
    header.numCores = config.numCores;
    header.perCoreTopNCapacity = config.perCoreTopN;
    header.shmTotalSize = layout.totalSize;
    header.registryOffset = layout.registryOffset;
    header.keyNamesOffset = layout.keyNamesOffset;
    header.keyModuleNamesOffset = layout.keyModuleNamesOffset;
    header.activeBitmapOffset = layout.activeBitmapOffset;
    header.shardsOffset = layout.shardsOffset;
    header.globalAccumOffset = layout.globalAccumOffset;
    header.globalAccumCount = config.maxSlots;
    header.spinlock.clear();
    const auto written = pwrite(fd, &header, sizeof(header), 0);
    const int closeResult = close(fd);
    return written == static_cast<ssize_t>(sizeof(header)) && closeResult == 0;
}

}

TEST(MultiShmNamespaceTest, TwoNamespacesAreIndependentAndCleanable) {
    using namespace SpDiag;
    const std::string suffix = std::to_string(getpid());
    const std::string nameA = "/spdiag_test_ns_a_" + suffix;
    const std::string nameB = "/spdiag_test_ns_b_" + suffix;
    const auto configA = MakeConfig(nameA);
    const auto configB = MakeConfig(nameB);
    SpDiagManager manager;
    struct Cleanup {
        SpDiagManager* manager;
        Cli::CliConfig a;
        Cli::CliConfig b;
        ~Cleanup() {
            (void)manager->StopShm(a);
            (void)manager->StopShm(b);
        }
    } cleanup{&manager, configA, configB};
    (void)manager.StopShm(configA);
    (void)manager.StopShm(configB);
    ASSERT_TRUE(manager.StartShm(configA));
    ASSERT_TRUE(manager.StartShm(configB));

    struct stat st {};
    ASSERT_EQ(stat(ShmFilesystemPath(nameA).c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600);

    for (const auto& item : {std::pair{nameA, "producer-a"},
                             std::pair{nameB, "producer-b"}}) {
        const pid_t child = fork();
        ASSERT_GE(child, 0);
        if (child == 0) _exit(RunProducer(ShmFilesystemPath(item.first),
                                         item.second));
        int status = 0;
        ASSERT_EQ(waitpid(child, &status, 0), child);
        ASSERT_TRUE(WIFEXITED(status));
        ASSERT_EQ(WEXITSTATUS(status), 0);
    }

    ShmReader readerA;
    ShmReader readerB;
    ASSERT_TRUE(readerA.Open(nameA));
    ASSERT_TRUE(readerB.Open(nameB));
    ASSERT_EQ(readerA.Header().registeredPrograms, 1u);
    ASSERT_EQ(readerB.Header().registeredPrograms, 1u);
    EXPECT_STREQ(readerA.ReadProgramRegistry()[0].name, "producer-a");
    EXPECT_STREQ(readerB.ReadProgramRegistry()[0].name, "producer-b");
    readerA.Close();
    readerB.Close();

    EXPECT_TRUE(manager.StopShm(configA));
    EXPECT_FALSE(manager.CheckShmAlive(nameA));
    EXPECT_TRUE(manager.CheckShmAlive(nameB));
    EXPECT_TRUE(manager.StopShm(configB));
    EXPECT_FALSE(manager.CheckShmAlive(nameB));
}

TEST(MultiShmNamespaceTest, CustomWatcherReconnectsAfterLateStart) {
    using namespace SpDiag;
    const std::string name = "/spdiag_test_late_" + std::to_string(getpid());
    const auto config = MakeConfig(name);
    SpDiagManager manager;
    struct Cleanup {
        SpDiagManager* manager;
        Cli::CliConfig config;
        ~Cleanup() { (void)manager->StopShm(config); }
    } cleanup{&manager, config};
    (void)manager.StopShm(config);
    int ready[2];
    ASSERT_EQ(pipe(ready), 0);
    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        close(ready[0]);
        _exit(RunLateAttachProducer(ShmFilesystemPath(name), ready[1]));
    }
    close(ready[1]);
    char signal = 0;
    ASSERT_EQ(read(ready[0], &signal, 1), 1);
    ASSERT_TRUE(PublishDelayedShm(config));
    close(ready[0]);
    int status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
    EXPECT_TRUE(manager.StopShm(config));
    EXPECT_FALSE(manager.CheckShmAlive(name));
}

TEST(MultiShmNamespaceTest, UnknownCustomObjectRequiresExplicitRemoval) {
    using namespace SpDiag;
    const std::string name =
        "/spdiag_test_foreign_" + std::to_string(getpid());
    ASSERT_TRUE(Platform::ShmDestroy(name));
    const int fd = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(ftruncate(fd, 4096), 0);
    ASSERT_EQ(close(fd), 0);

    auto config = MakeConfig(name);
    SpDiagManager manager;
    EXPECT_FALSE(manager.StartShm(config));
    EXPECT_FALSE(manager.StopShm(config));
    EXPECT_FALSE(manager.RestartShm(config));
    EXPECT_EQ(Platform::ShmCheckExistence(name), ShmExistence::PRESENT);

    config.forceRemoveShm = true;
    ASSERT_TRUE(manager.StopShm(config));
    EXPECT_EQ(Platform::ShmCheckExistence(name), ShmExistence::ABSENT);
}
