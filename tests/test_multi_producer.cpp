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
#include <algorithm>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "cli/cli_config.h"
#include "manager/aggregator.h"
#include "manager/manager.h"
#include "runtime/shm_reader.h"
#include "spdiag/perf_config.h"
#include "spdiag/perf_key_defined.h"
#include "spdiag/perf_manager.h"
#include "spdiag/perf_point.h"

namespace {

void ProducerChild(const char* shmName, int readyFd, int startFd) {
    SpDiag::PerfConfig config;
    config.shmName = shmName;
    config.programName = "same-producer";
    config.levelGate = 4;
    config.dumpIntervalMs = 0;
    SpDiag::PerfManager::Instance().Init(config);
    if (!SpDiag::PerfManager::Instance().IsInitialized()) _exit(11);
    char signal = 'R';
    if (write(readyFd, &signal, 1) != 1 || read(startFd, &signal, 1) != 1)
        _exit(12);
    SpDiag::PerfPoint point(SpDiag::PerfKeyDefined::COMPUTE_MATMUL,
                            SpDiag::PerfLevel::KEY_MODULE);
    point.Start();
    point.End(0);
    SpDiag::PerfManager::Instance().Shutdown();
    _exit(0);
}

}  // namespace

TEST(MultiProducerTest, SameNameProcessesUseIndependentSlotsAndAggregate) {
    using namespace SpDiag;
    const std::string shmName = "/spdiag_test_multi_" + std::to_string(getpid());
    Cli::CliConfig config;
    config.shmName = shmName;
    config.numCores = 2;
    config.maxSlots = 2 * MAX_PERF_SLOTS;
    config.maxPrograms = 4;
    config.dumpIntervalMs = 0;
    config.levelGate = 4;
    SpDiagManager manager;
    (void)manager.StopShm(config);
    ASSERT_TRUE(manager.StartShm(config));
    struct Cleanup {
        SpDiagManager* manager;
        Cli::CliConfig config;
        ~Cleanup() { manager->StopShm(config); }
    } cleanup{&manager, config};

    int ready[2], start[2];
    ASSERT_EQ(pipe(ready), 0);
    ASSERT_EQ(pipe(start), 0);
    pid_t children[2];
    for (pid_t& child : children) {
        child = fork();
        ASSERT_GE(child, 0);
        if (child == 0) ProducerChild(shmName.c_str(), ready[1], start[0]);
    }
    close(ready[1]);
    close(start[0]);
    char signal[2];
    ASSERT_EQ(read(ready[0], signal, 1), 1);
    ASSERT_EQ(read(ready[0], signal + 1, 1), 1);
    ASSERT_EQ(write(start[1], "GG", 2), 2);
    close(ready[0]);
    close(start[1]);
    for (pid_t child : children) {
        int status = 0;
        ASSERT_EQ(waitpid(child, &status, 0), child);
        ASSERT_TRUE(WIFEXITED(status));
        ASSERT_EQ(WEXITSTATUS(status), 0);
    }

    ShmReader reader;
    ASSERT_TRUE(reader.Open(shmName));
    ASSERT_EQ(reader.Header().registeredPrograms, 2u);
    const ProgramEntry* programs = reader.ReadProgramRegistry();
    EXPECT_NE(programs[0].baseSlot, programs[1].baseSlot);
    EXPECT_STREQ(programs[0].name, programs[1].name);
    ShardBuffer shards;
    ASSERT_TRUE(reader.SnapshotShards(shards));
    Aggregator aggregator;
    aggregator.Aggregate(programs, 2, reader.ReadKeyNames(),
                         reader.ReadKeyModuleNames(), shards,
                         reader.Header().numCores);
    const auto it = std::find_if(
        aggregator.Results().begin(), aggregator.Results().end(),
        [](const AggregatedStat& stat) { return stat.pointName == "MatMul"; });
    ASSERT_NE(it, aggregator.Results().end());
    EXPECT_EQ(it->programName, "same-producer");
    EXPECT_EQ(it->totalTickCount, 2u);
    EXPECT_EQ(it->totalGoodCount, 2u);
}
