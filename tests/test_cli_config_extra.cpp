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
#include "src/cli/cli_config.h"
#include <fstream>
#include <cstdio>

using namespace SpDiag;
using namespace SpDiag::Cli;

static std::string WriteTempConfig(const std::string& content) {
    std::string path = "test_cli_config_extra_temp.conf";
    std::ofstream ofs(path);
    ofs << content;
    ofs.close();
    return path;
}

TEST(CliConfigExtraTest, ParseConfigFile_FullKeys) {
    const char* configContent = R"(
[shm]
name = /my_shm

[general]
version = 5
data_dir = /var/lib/spdiag
history_size = 100
dump_interval_ms = 2000
level_gate = 3
num_cores = 16

[perf]
maxSlots = 2048
maxPrograms = 32
)";
    std::string path = WriteTempConfig(configContent);
    CliConfig cfg = CliConfig::LoadFromFile(path, true);
    std::remove(path.c_str());

    EXPECT_EQ(cfg.shmName, "/my_shm");
    EXPECT_EQ(cfg.expectedVersion, 5u);
    EXPECT_EQ(cfg.dataDir, "/var/lib/spdiag");
    EXPECT_EQ(cfg.historySize, 100u);
    EXPECT_EQ(cfg.dumpIntervalMs, 2000u);
    EXPECT_EQ(cfg.levelGate, 3);
    EXPECT_EQ(cfg.numCores, 16u);
    EXPECT_EQ(cfg.maxSlots, 2048u);
    EXPECT_EQ(cfg.maxPrograms, 32u);
}

TEST(CliConfigExtraTest, ParseConfigFile_PartialKeys) {
    const char* configContent = R"(
[shm]
name = /partial_shm

[general]
version = 2

[perf]
maxSlots = 512
)";
    std::string path = WriteTempConfig(configContent);
    CliConfig cfg = CliConfig::LoadFromFile(path, true);
    std::remove(path.c_str());

    EXPECT_EQ(cfg.shmName, "/partial_shm");
    EXPECT_EQ(cfg.expectedVersion, 2u);
    EXPECT_EQ(cfg.maxSlots, 512u);

    EXPECT_EQ(cfg.dataDir, "/tmp/spdiag_data");
    EXPECT_EQ(cfg.historySize, 30u);
    EXPECT_EQ(cfg.dumpIntervalMs, 0u);
    EXPECT_EQ(cfg.levelGate, 0);
    EXPECT_EQ(cfg.numCores, 0u);
    EXPECT_EQ(cfg.maxPrograms, 8u);
}

TEST(CliConfigExtraTest, ConfigFileNotFound) {
    CliConfig cfg = CliConfig::LoadFromFile("/nonexistent/path/spdiag.conf", true);

    EXPECT_EQ(cfg.shmName, "/spdiag_shm_default");
    EXPECT_EQ(cfg.expectedVersion, 1u);
    EXPECT_EQ(cfg.dataDir, "/tmp/spdiag_data");
    EXPECT_EQ(cfg.historySize, 30u);
    EXPECT_EQ(cfg.dumpIntervalMs, 0u);
    EXPECT_EQ(cfg.levelGate, 0);
    EXPECT_EQ(cfg.numCores, 0u);
    EXPECT_EQ(cfg.maxSlots, 1024u);
    EXPECT_EQ(cfg.maxPrograms, 8u);
}

TEST(CliConfigExtraTest, ConfigFile_CommentsAndBlanks) {
    const char* configContent = R"(
# This is a comment line

[general]
version = 99     # inline comment after value

data_dir = /custom/data   ; semicolon comment

history_size = 50

# Another section follows

[perf]
maxSlots = 128
maxPrograms = 4
# end of file
)";
    std::string path = WriteTempConfig(configContent);
    CliConfig cfg = CliConfig::LoadFromFile(path, true);
    std::remove(path.c_str());

    EXPECT_EQ(cfg.expectedVersion, 99u);
    EXPECT_EQ(cfg.dataDir, "/custom/data");
    EXPECT_EQ(cfg.historySize, 50u);
    EXPECT_EQ(cfg.maxSlots, 128u);
    EXPECT_EQ(cfg.maxPrograms, 4u);

    EXPECT_EQ(cfg.shmName, "/spdiag_shm_default");
    EXPECT_EQ(cfg.dumpIntervalMs, 0u);
    EXPECT_EQ(cfg.levelGate, 0);
    EXPECT_EQ(cfg.numCores, 0u);
}
