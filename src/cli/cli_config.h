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

#pragma once
#include <string>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace SpDiag {

enum class LevelOp {
    LT,
    LE,
    EQ,
    GE,
    GT
};

namespace Cli {

enum class Command {
    SHOW,
    WATCH,
    START,
    STOP,
    RESTART,
    STATUS,
    CLEAR,
    HISTORY,
    MEMSTAT,
    CACHESTAT,
    MEMPOINT,
};

const char* CommandName(Command cmd);

enum class CommandCategory {
    SHM_SIMPLE,
    HISTORY,
    SHOW,
    WATCH,
    OBSERVATION,
};

CommandCategory GetCategory(Command cmd);

enum class UserFlag : uint32_t {
    DETAIL,
    CORE,
    LEVEL,
    SORT,
    ASC_DESC,
    INTERVAL,
    HISTORY_N,
    HISTORY_I,
    STRICT_SNAP,
    PID,
    LIBC_PATH,
    CACHE_LEVELS,
    NO_FILTER,
    BYTES,
    GROUP_BY,
    TOP,
    SHOW_FIELDS,
    UB_DELTA,
    CSV_OUTPUT,
};

constexpr uint32_t FlagBit(UserFlag f) {
    return 1u << static_cast<uint32_t>(f);
}

constexpr uint32_t FlagBits(std::initializer_list<UserFlag> flags) {
    uint32_t mask = 0;
    for (UserFlag f : flags) {
        mask |= FlagBit(f);
    }
    return mask;
}

struct CategoryRule {
    CommandCategory category;
    uint32_t        allowedFlags;
    uint32_t        requiredFlags;
};

struct CliConfig {
    Command     command         = Command::SHOW;
    std::string shmName         = "/spdiag_shm_default";
    uint32_t    expectedVersion = 1;
    std::string configPath;
    std::string logLevelStr;        // --log-level 参数值 (DEBUG/INFO/WARN/ERROR)

    bool        showDetail      = false;
    int32_t     coreStart       = -1;
    int32_t     coreEnd         = -1;
    uint8_t     levelFilter     = 0;
    LevelOp     levelOp         = LevelOp::LE;
    bool        hasLevelRange   = false;
    uint8_t     levelRangeStart = 0;
    uint8_t     levelRangeEnd   = 0;
    std::string sortSpec;
    std::vector<std::string> filterGlobs;
    uint32_t    watchIntervalMs = 2000;
    bool        strictSnapshot  = false;
#ifdef SPDIAG_ENABLE_PERFLOG
    bool        perflogShow     = false;
    bool        perflogStart    = false;
#endif

    int32_t     obTargetPid     = -1;
    uint32_t    obIntervalMs      = 1000;
    std::string obLibcPath = "/usr/lib64/libc.so.6";
    std::vector<std::string> obCacheLevels = {"L1-dcache", "L1-icache", "LLC"};
    bool        obHumanReadable = true;
    bool        obNoFilter      = false;
    bool        obDetail        = false;
    bool        obCumulative    = true;
    std::string obGroupBy       = "tid+frame";
    int         obTop           = 20;
    std::string obShowFields;

    uint32_t    historySize     = 30;
    std::string dataDir         = "/tmp/spdiag_data";
    std::string csvOutputDir = "./results";  // --csv [dir], default ./results
    int32_t     historyCount    = -1;
    int32_t     historyIndex    = -1;
    bool        clearHistory    = false;
    uint32_t    dumpIntervalMs = 0;
    uint8_t     levelGate      = 0;
    uint32_t    numCores       = 0;

    uint32_t    maxSlots       = 1024;
    uint32_t    maxPrograms    = 8;

    uint32_t    userFlags      = 0;

    void SetFlag(UserFlag f) { userFlags |= FlagBit(f); }
    bool HasAnyFlag(uint32_t mask) const { return (userFlags & mask) != 0; }

    static CliConfig ParseArgs(int argc, char* argv[]);
    static CliConfig LoadFromFile(const std::string& path, bool silent = false);
};

struct ParseResult {
    CliConfig config;
    std::string error;
    bool helpRequested = false;
    bool versionRequested = false;
    bool commandSpecified = false;
};

ParseResult ParseArgsInternal(int argc, char* argv[]);

}

}