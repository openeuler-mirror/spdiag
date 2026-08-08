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

#include "cli_config.h"
#include "_build_info.h"
#include "sort_spec.h"
#include "runtime/plugin_types.h"
#include "spdiag/version.h"
#include "spdiag/log_macros.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <sstream>

namespace {

using SpDiag::Cli::Command;
using SpDiag::Cli::CommandCategory;
using SpDiag::Cli::CategoryRule;
using SpDiag::Cli::ParseResult;
using SpDiag::Cli::UserFlag;
using SpDiag::Cli::FlagBit;
using SpDiag::Cli::FlagBits;
using SpDiag::Cli::CommandName;
using SpDiag::Cli::GetCategory;
using SpDiag::Cli::CliConfig;
using SpDiag::Cli::ParseSortSpec;

ParseResult MakeError(const std::string& msg) {
    ParseResult result;
    result.error = msg;
    return result;
}

namespace FlagGroups {
    static const uint32_t SHOW_WATCH = FlagBits({
        UserFlag::DETAIL, UserFlag::CORE, UserFlag::LEVEL, UserFlag::SORT,
        UserFlag::ASC_DESC, UserFlag::STRICT_SNAP, UserFlag::CSV_OUTPUT
    });

    static const uint32_t OBSERVATION = FlagBits({
        UserFlag::PID, UserFlag::NO_FILTER, UserFlag::GROUP_BY,
        UserFlag::TOP, UserFlag::SHOW_FIELDS, UserFlag::DETAIL,
        UserFlag::LIBC_PATH, UserFlag::CACHE_LEVELS, UserFlag::UB_DELTA,
        UserFlag::CSV_OUTPUT
    });

    static const uint32_t HISTORY_ONLY = FlagBits({
        UserFlag::HISTORY_N, UserFlag::HISTORY_I, UserFlag::CSV_OUTPUT
    });
}

static const CommandCategory COMMAND_CATEGORY_MAP[] = {
    CommandCategory::SHOW,        // SHOW
    CommandCategory::WATCH,       // WATCH
    CommandCategory::SHM_SIMPLE,  // START
    CommandCategory::SHM_SIMPLE,  // STOP
    CommandCategory::SHM_SIMPLE,  // RESTART
    CommandCategory::SHM_SIMPLE,  // STATUS
    CommandCategory::SHM_SIMPLE,  // CLEAR
    CommandCategory::HISTORY,     // HISTORY
    CommandCategory::OBSERVATION, // MEMSTAT
    CommandCategory::OBSERVATION, // CACHESTAT
    CommandCategory::OBSERVATION, // MEMPOINT
};

static const CategoryRule CATEGORY_RULES[] = {
    { CommandCategory::SHM_SIMPLE,  0,                                                      0 },
    { CommandCategory::HISTORY,     FlagGroups::HISTORY_ONLY,                                0 },
    { CommandCategory::SHOW,        FlagGroups::SHOW_WATCH,                                  0 },
    { CommandCategory::WATCH,       FlagGroups::SHOW_WATCH | FlagBit(UserFlag::INTERVAL),    0 },
    { CommandCategory::OBSERVATION, FlagGroups::OBSERVATION | FlagBit(UserFlag::INTERVAL) | FlagBit(UserFlag::BYTES),
                                    FlagBit(UserFlag::PID) },
};

const CategoryRule* FindRule(CommandCategory cat) {
    for (const auto& rule : CATEGORY_RULES) {
        if (rule.category == cat) return &rule;
    }
    return nullptr;
}

struct CommandEntry {
    const char* name;
    Command     cmd;
    uint32_t    features;
};

static const CommandEntry COMMAND_ENTRIES[] = {
    {"show",      Command::SHOW,      0              },
    {"watch",     Command::WATCH,     0              },
    {"start",     Command::START,     0              },
    {"stop",      Command::STOP,      0              },
    {"restart",   Command::RESTART,   0              },
    {"status",    Command::STATUS,    0              },
    {"clear",     Command::CLEAR,     0              },
    {"history",   Command::HISTORY,   0              },
#ifdef SPDIAG_ENABLE_OB_MEMORY
    {"memstat",   Command::MEMSTAT,   SpDiag::FEAT_OB_MEMORY },
#endif
#ifdef SPDIAG_ENABLE_OB_CACHE
    {"cachestat", Command::CACHESTAT, SpDiag::FEAT_OB_CACHE  },
#endif
#ifdef SPDIAG_ENABLE_MEMPOINT
    {"mempoint",  Command::MEMPOINT,  SpDiag::FEAT_MEMPOINT  },
#endif
};

const CommandEntry* FindCommand(const std::string& arg) {
    for (const auto& entry : COMMAND_ENTRIES) {
        if (arg == entry.name) return &entry;
    }
    return nullptr;
}

std::string ValidateCommandOptions(const CliConfig& cfg) {
    const CategoryRule* rule = FindRule(GetCategory(cfg.command));
    if (!rule) return {};

    uint32_t disallowed = cfg.userFlags & ~rule->allowedFlags;
    if (disallowed) {
        return "Option not applicable to '" + std::string(CommandName(cfg.command)) + "' command";
    }

    uint32_t missing = rule->requiredFlags & ~cfg.userFlags;
    if (missing & FlagBit(UserFlag::PID)) {
        return "Invalid value for --pid: required for " + std::string(CommandName(cfg.command));
    }

    if (!cfg.sortSpec.empty()) {
        std::string sortErr;
        std::string sortMode;
        if (cfg.command == Command::MEMSTAT || cfg.command == Command::MEMPOINT) {
            sortMode = "memstat";
        } else {
            sortMode = "perfpoint";
        }
        if (ParseSortSpec(cfg.sortSpec, sortMode, sortErr).empty() && !sortErr.empty()) {
            return "Invalid --sort: " + sortErr;
        }
    }

    return {};
}

void TrimInPlace(std::string& s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.erase(s.begin());
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
        s.pop_back();
    }
}

std::string ToLowerCopy(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool FileExists(const std::string& path) {
    std::ifstream ifs(path);
    return ifs.is_open();
}

void MergeConfig(SpDiag::Cli::CliConfig& dst, const SpDiag::Cli::CliConfig& src) {
    if (!src.shmName.empty()) {
        dst.shmName = src.shmName;
    }
    if (src.expectedVersion != 1) {
        dst.expectedVersion = src.expectedVersion;
    }
    if (!src.dataDir.empty()) {
        dst.dataDir = src.dataDir;
    }
    if (src.historySize != 30) {
        dst.historySize = src.historySize;
    }
    if (src.dumpIntervalMs != 0) {
        dst.dumpIntervalMs = src.dumpIntervalMs;
    }
    if (src.levelGate != 0) {
        dst.levelGate = src.levelGate;
    }
    if (src.numCores != 0) {
        dst.numCores = src.numCores;
    }
    if (src.maxSlots != 1024) {
        dst.maxSlots = src.maxSlots;
    }
    if (src.maxPrograms != 8) {
        dst.maxPrograms = src.maxPrograms;
    }
}

bool ParseUint8Value(const std::string& text, uint8_t& out, std::string& error) {
    if (text.empty()) {
        error = "empty number";
        return false;
    }

    size_t parsed = 0;
    int value = 0;
    try {
        value = std::stoi(text, &parsed);
    } catch (const std::exception& e) {
        error = "not a valid integer: '" + text + "'";
        SPDIAG_LOG_ERROR(SpDiag::LogModule::CLI, "ParseUint8Value failed: %s, input: '%s', exception: %s", 
                         error.c_str(), text.c_str(), e.what());
        return false;
    }

    if (parsed != text.size()) {
        error = "contains non-numeric characters: '" + text + "'";
        return false;
    }

    if (value < 0 || value > 255) {
        error = "out of range [0,255]: '" + text + "'";
        return false;
    }

    out = static_cast<uint8_t>(value);
    return true;
}

bool ParseInt32Value(const std::string& text, int32_t& out, std::string& error) {
    if (text.empty()) {
        error = "empty number";
        return false;
    }

    size_t parsed = 0;
    long value = 0;
    try {
        value = std::stol(text, &parsed);
    } catch (const std::exception& e) {
        error = "not a valid integer: '" + text + "'";
        SPDIAG_LOG_ERROR(SpDiag::LogModule::CLI, "ParseInt32Value failed: %s, input: '%s', exception: %s", 
                         error.c_str(), text.c_str(), e.what());
        return false;
    }

    if (parsed != text.size()) {
        error = "contains non-numeric characters: '" + text + "'";
        return false;
    }

    if (value < static_cast<long>(INT32_MIN) || value > static_cast<long>(INT32_MAX)) {
        error = "out of int32 range: '" + text + "'";
        return false;
    }

    out = static_cast<int32_t>(value);
    return true;
}

std::vector<std::string> ParseCsvList(const std::string& text) {
    std::vector<std::string> items;
    std::stringstream ss(text);
    std::string token;
    while (std::getline(ss, token, ',')) {
        TrimInPlace(token);
        if (!token.empty()) {
            items.push_back(token);
        }
    }
    return items;
}

bool ParseLevelExpr(const std::string& levelArg, SpDiag::Cli::CliConfig& cfg, std::string& error) {
    cfg.hasLevelRange = false;

    auto parseSingleValue = [&](SpDiag::LevelOp op, const std::string& numPart) -> bool {
        uint8_t value = 0;
        if (!ParseUint8Value(numPart, value, error)) {
            return false;
        }
        cfg.levelOp = op;
        cfg.levelFilter = value;
        return true;
    };

    if (levelArg.rfind(">=", 0) == 0) {
        return parseSingleValue(SpDiag::LevelOp::GE, levelArg.substr(2));
    }
    if (levelArg.rfind("<=", 0) == 0) {
        return parseSingleValue(SpDiag::LevelOp::LE, levelArg.substr(2));
    }
    if (levelArg.rfind(">", 0) == 0) {
        return parseSingleValue(SpDiag::LevelOp::GT, levelArg.substr(1));
    }
    if (levelArg.rfind("<", 0) == 0) {
        return parseSingleValue(SpDiag::LevelOp::LT, levelArg.substr(1));
    }
    if (levelArg.rfind("=", 0) == 0) {
        return parseSingleValue(SpDiag::LevelOp::EQ, levelArg.substr(1));
    }

    const auto dashPos = levelArg.find('-');
    if (dashPos != std::string::npos) {
        if (dashPos == 0 || dashPos + 1 >= levelArg.size()) {
            error = "range must be N-M, for example 2-3";
            return false;
        }
        if (levelArg.find('-', dashPos + 1) != std::string::npos) {
            error = "range must contain only one '-' separator";
            return false;
        }

        uint8_t start = 0;
        uint8_t end = 0;
        if (!ParseUint8Value(levelArg.substr(0, dashPos), start, error)) {
            return false;
        }
        if (!ParseUint8Value(levelArg.substr(dashPos + 1), end, error)) {
            return false;
        }
        if (start > end) {
            error = "range start must be <= end";
            return false;
        }

        cfg.hasLevelRange = true;
        cfg.levelRangeStart = start;
        cfg.levelRangeEnd = end;
        cfg.levelFilter = 0;
        cfg.levelOp = SpDiag::LevelOp::LE;
        return true;
    }

    return parseSingleValue(SpDiag::LevelOp::LE, levelArg);
}

struct GlobalScanResult {
    std::string configPath;
    bool helpRequested = false;
    bool configError = false;
};

GlobalScanResult ScanGlobalOptions(int argc, char* argv[], CliConfig& cfg) {
    GlobalScanResult result;
    bool configPathExplicit = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            result.helpRequested = true;
            return result;
        }
        if (arg == "--config") {
            if (i + 1 >= argc) {
                result.configError = true;
                return result;
            }
            result.configPath = argv[++i];
            configPathExplicit = true;
        }
    }

    if (!configPathExplicit) {
        constexpr std::array<const char*, 3> defaultConfigPaths = {
            "config/spdiag.conf",
            "spdiag.conf",
            "/etc/spdiag/spdiag.conf"
        };
        for (const char* candidate : defaultConfigPaths) {
            if (FileExists(candidate)) {
                result.configPath = candidate;
                break;
            }
        }
    }

    if (!result.configPath.empty()) {
        cfg.configPath = result.configPath;
        CliConfig fileConfig = CliConfig::LoadFromFile(result.configPath, !configPathExplicit);
        MergeConfig(cfg, fileConfig);
    }

    return result;
}

}

namespace SpDiag {
namespace Cli {

const char* CommandName(Command cmd) {
    switch (cmd) {
        case Command::SHOW:      return "show";
        case Command::WATCH:     return "watch";
        case Command::START:     return "start";
        case Command::STOP:      return "stop";
        case Command::RESTART:   return "restart";
        case Command::STATUS:    return "status";
        case Command::CLEAR:     return "clear";
        case Command::HISTORY:   return "history";
        case Command::MEMSTAT:   return "memstat";
        case Command::CACHESTAT: return "cachestat";
        case Command::MEMPOINT:  return "mempoint";
    }
    return "unknown";
}

CommandCategory GetCategory(Command cmd) {
    return COMMAND_CATEGORY_MAP[static_cast<int>(cmd)];
}

static void PrintUsage() {
    std::cerr <<
        "Usage: spdiag <command> [options]\n"
        "\n"
        "Options:\n"
        "  -h, --help             Show this help message\n"
        "  --version              Show version\n"
        "\n"
        "Commands:\n"
        "  start          Create shared memory for spdiag\n"
        "  stop           Destroy shared memory\n"
        "  clear          Clear performance data\n"
        "  restart        Recreate shared memory\n"
        "  status         Show shared memory status\n"
        "  show           Show performance statistics\n"
        "  watch          Continuously monitor statistics\n"
        "  history        Show dump history records\n"
#ifdef SPDIAG_ENABLE_OB_MEMORY
        "  memstat         Monitor memory allocation of a process (Linux only)\n"
#endif
#ifdef SPDIAG_ENABLE_OB_CACHE
        "  cachestat       Monitor cache hit rate of a process (Linux only)\n"
#endif
#ifdef SPDIAG_ENABLE_MEMPOINT
        "  mempoint        Monitor point-level memory of a process (Linux only)\n"
#endif
        "\n"
        "Show/Watch Options:\n"
        "  -d, --detail           Show per-core detail (show/watch only)\n"
        "  -c, --core N[-M]       Filter by core range\n"
        "  -l, --level EXPR       Filter by level (1-4)\n"
        "                        Operators: >, <, >=, <=, = (default: <=)\n"
        "                        Supports range N-M, e.g. 2-3\n"
        "                        Quote >/< in shell, e.g. --level \">2\"\n"
        "  --interval <ms>    Watch/collection interval in milliseconds (default: 2000)\n"
        "  --strict-snapshot      Fail if snapshot consistency check fails\n"
        "  --perflog              Enable perflog timestamp logging (start)\n"
        "                         Show perflog timestamp log (show/watch)\n"
        "\n"
        "  -H, --clear-history    Clear dump history records\n"
        "  --history-size <N>     History ring buffer capacity (default: 30)\n"
        "  --data-dir <path>      Data directory for persistent files\n"
        "  --csv [dir]            Export output as CSV files (default: ./results)\n"
        "\n"
        "Sorting & Filtering (show/watch, memstat, and mempoint):\n"
        "  -s, --sort KEY[:DIR][,...]  Multi-key sort (leftmost = highest priority)\n"
        "                        DIR: asc or desc (omit = per-key default)\n"
        "                        PerfPoint keys: name, lvl, ticks, good, bad, total,\n"
        "                            avg, min, max  (default: name:asc)\n"
        "                        Memstat/MemPoint keys: current, peak, total, allocs, frees,\n"
        "                            max_size, min_size, first_seen, leaked, avg_size,\n"
        "                            frame  (default: current,peak,allocs)\n"
        "  --filter PAT[,PAT]...  Glob filter (* and ?). Keep entries matching any PAT.\n"
        "                        PerfPoint: matches Module.Point. Memstat: matches frame.\n"
        "                        MemPoint: matches category.key_name.\n"
        "\n"
        "OB Options:\n"
        "  --pid <N>              Target process PID (required for observation commands)\n"
        "  --libc-path <path>     Path to libc.so (default: /usr/lib64/libc.so.6)\n"
        "  --cache-levels <list>  Cache levels, comma-separated (default: L1-dcache,L1-icache,LLC)\n"
        "  --interval <ms>        Collection interval in milliseconds (default: 1000)\n"
        "  --delta               Show per-second delta instead of cumulative totals\n"
        "                        (cachestat only)\n"
        "  --bytes               Show raw integer bytes instead of human-readable\n"
        "  --no-filter            Show raw stack without frame filtering\n"
        "  --detail              Show per-record call-stack tree\n"
        "  --group-by {tid+frame,frame,raw}  Aggregation mode. Default: tid+frame\n"
        "  --top <N>             Show top N groups + [others] row. Default: 20\n"
        "  --show COL[,COL]...    Show only these columns (replaces defaults)\n"
        "                        PID, TID, CallStack are always shown\n"
        "                        Available: current, total, allocs, frees, peak,\n"
        "                            min_size, max_size, first_seen, leaked,\n"
        "                            avg_size, churn, churn_rate, alloc_rate\n"
        "\n"
        "Compilation notes for target program (for complete stack traces):\n"
        "  CFLAGS   += -g -fno-omit-frame-pointer -fasynchronous-unwind-tables\n"
        "  CXXFLAGS += -g -fno-omit-frame-pointer -fasynchronous-unwind-tables\n"
        "  LDFLAGS  += -Wl,--build-id=sha1\n"
        "  # Do not strip debug sections (.symtab, .debug_line, .debug_info)\n"
        "  # Without -g, source file:line info will not be available\n";
}

ParseResult ParseArgsInternal(int argc, char* argv[]) {
    ParseResult result;
    CliConfig& cfg = result.config;

    if (argc <= 1) {
        result.helpRequested = true;
        return result;
    }

    // Phase 1: scan global options and load config
    auto global = ScanGlobalOptions(argc, argv, cfg);
    if (global.helpRequested) {
        result.helpRequested = true;
        return result;
    }
    if (global.configError) {
        return MakeError("Missing value for --config");
    }

    bool commandSet = false;

    int i = 1;
    while (i < argc) {
        std::string arg = argv[i];

        if (arg == "--config") {
            return MakeError("Error: --config option is temporarily disabled");
        } else if (arg == "--shm") {
            return MakeError("Error: --shm option is temporarily disabled");
        } else if (arg == "--version") {
            result.versionRequested = true;
            return result;
        } else if (arg == "--pid") {
            if (i + 1 >= argc) {
                return MakeError("Invalid value for --pid: missing value");
            }

            std::string pidArg = argv[++i];
            std::string error;
            int32_t pid = -1;
            if (!ParseInt32Value(pidArg, pid, error) || pid <= 0) {
                return MakeError("Invalid value for --pid: " + error);
            }
            cfg.obTargetPid = pid;
            cfg.SetFlag(UserFlag::PID);
        } else if (const CommandEntry* entry = FindCommand(arg)) {
            if (commandSet) {
                return MakeError("Multiple commands specified: unexpected '" + arg + "'");
            }
            cfg.command = entry->cmd;
            commandSet = true;
            result.commandSpecified = true;
        } else if (arg == "-d" || arg == "--detail") {
            cfg.showDetail = true;
            cfg.obDetail = true;
            cfg.SetFlag(UserFlag::DETAIL);
        } else if (arg == "-H" || arg == "--clear-history") {
            cfg.clearHistory = true;
        } else if (arg == "--history-size") {
            if (i + 1 >= argc) {
                return MakeError("Invalid value for --history-size: missing value");
            }

            std::string hsArg = argv[++i];
            try {
                uint32_t hs = static_cast<uint32_t>(std::stoul(hsArg));
                if (hs == 0) {
                    return MakeError("Invalid value for --history-size: must be greater than 0");
                }
                cfg.historySize = hs;
            } catch (const std::exception& e) {
                SPDIAG_LOG_ERROR(SpDiag::LogModule::CLI, "Parse --history-size failed: '%s', exception: %s", 
                                 hsArg.c_str(), e.what());
                return MakeError("Invalid value for --history-size: '" + hsArg + "' is not a valid number");
            }
        } else if (arg == "--data-dir") {
            if (i + 1 >= argc) {
                return MakeError("Invalid value for --data-dir: missing value");
            }
            cfg.dataDir = argv[++i];
        } else if (arg == "-n") {
            if (i + 1 >= argc) {
                return MakeError("Invalid value for -n: missing value");
            }

            std::string nArg = argv[++i];
            std::string error;
            int32_t nVal = -1;
            if (!ParseInt32Value(nArg, nVal, error) || nVal <= 0) {
                return MakeError("Invalid value for -n: " + error);
            }
            cfg.historyCount = nVal;
            cfg.SetFlag(UserFlag::HISTORY_N);
        } else if (arg == "-i") {
            if (i + 1 >= argc) {
                return MakeError("Invalid value for -i: missing value");
            }

            std::string iArg = argv[++i];
            std::string error;
            int32_t iVal = -1;
            if (!ParseInt32Value(iArg, iVal, error) || iVal < 0) {
                return MakeError("Invalid value for -i: " + error);
            }
            cfg.historyIndex = iVal;
            cfg.SetFlag(UserFlag::HISTORY_I);
        } else if (arg == "-c" || arg == "--core") {
            if (i + 1 >= argc) {
                return MakeError("Invalid value for --core: missing value");
            }

            std::string coreArg = argv[++i];
            auto dashPos = coreArg.find('-');

            try {
                if (dashPos != std::string::npos) {
                    cfg.coreStart = static_cast<int32_t>(std::stoi(coreArg.substr(0, dashPos)));
                    cfg.coreEnd = static_cast<int32_t>(std::stoi(coreArg.substr(dashPos + 1)));
                } else {
                    cfg.coreStart = static_cast<int32_t>(std::stoi(coreArg));
                    cfg.coreEnd = cfg.coreStart;
                }
            } catch (const std::exception& e) {
                SPDIAG_LOG_ERROR(SpDiag::LogModule::CLI, "Parse --core failed: '%s', exception: %s", 
                                 coreArg.c_str(), e.what());
                return MakeError("Invalid value for --core: '" + coreArg + "' is not a valid integer");
            }

            if (cfg.coreStart < 0 || cfg.coreEnd < 0) {
                return MakeError("Invalid value for --core: core number must be >= 0");
            }
            if (cfg.coreStart > cfg.coreEnd) {
                return MakeError("Invalid value for --core: range start must be <= end");
            }
            cfg.SetFlag(UserFlag::CORE);
        } else if (arg == "-l" || arg == "--level") {
            if (i + 1 >= argc || argv[i + 1] == nullptr) {
                return MakeError("Invalid value for --level: missing value");
            }

            std::string levelArg = argv[++i];
            if (levelArg.empty()) {
                return MakeError("Invalid value for --level: empty expression");
            }

            std::string error;
            if (!ParseLevelExpr(levelArg, cfg, error)) {
                return MakeError("Invalid value for --level: " + error);
            }
            cfg.SetFlag(UserFlag::LEVEL);
        } else if (arg == "-s" || arg == "--sort") {
            if (i + 1 >= argc) {
                return MakeError("Invalid value for --sort: missing value");
            }
            cfg.sortSpec = argv[++i];
        } else if (arg == "--filter") {
            if (i + 1 >= argc) {
                return MakeError("Invalid value for --filter: missing value");
            }
            std::string filterArg = argv[++i];
            // Split by comma
            size_t fp = 0;
            while (fp < filterArg.size()) {
                size_t fc = filterArg.find(',', fp);
                std::string pat = (fc == std::string::npos)
                    ? filterArg.substr(fp)
                    : filterArg.substr(fp, fc - fp);
                if (!pat.empty()) cfg.filterGlobs.push_back(std::move(pat));
                if (fc == std::string::npos) break;
                fp = fc + 1;
            }
        } else if (arg == "--interval") {
            if (i + 1 >= argc) {
                return MakeError("Invalid value for --interval: missing value");
            }

            std::string intervalArg = argv[++i];
            try {
                uint32_t interval = static_cast<uint32_t>(std::stoul(intervalArg));
                if (interval == 0) {
                    return MakeError("Invalid value for --interval: must be greater than 0");
                }
                cfg.watchIntervalMs = interval;
                cfg.obIntervalMs = interval;
                cfg.SetFlag(UserFlag::INTERVAL);
            } catch (const std::exception& e) {
                SPDIAG_LOG_ERROR(SpDiag::LogModule::CLI, "Parse --interval failed: '%s', exception: %s", 
                                 intervalArg.c_str(), e.what());
                return MakeError("Invalid value for --interval: '" + intervalArg + "' is not a valid number");
            }
        } else if (arg == "--libc-path") {
            if (i + 1 >= argc) {
                return MakeError("Invalid value for --libc-path: missing value");
            }
            cfg.obLibcPath = argv[++i];
            cfg.SetFlag(UserFlag::LIBC_PATH);
        } else if (arg == "--cache-levels") {
            if (i + 1 >= argc) {
                return MakeError("Invalid value for --cache-levels: missing value");
            }
            auto levels = ParseCsvList(argv[++i]);
            static const std::array<std::string, 6> validLevels = {"L1-dcache", "L1-icache", "LLC", "dTLB", "iTLB", "branch"};
            for (const auto& lvl : levels) {
                if (std::find(validLevels.begin(), validLevels.end(), lvl) == validLevels.end()) {
                    return MakeError("Invalid value for --cache-levels: '" + lvl + "' is not valid (valid: L1-dcache, L1-icache, LLC, dTLB, iTLB, branch)");
                }
            }
            cfg.obCacheLevels = levels;
            cfg.SetFlag(UserFlag::CACHE_LEVELS);
        } else if (arg == "--no-filter") {
            cfg.obNoFilter = true;
        } else if (arg == "--bytes") {
            cfg.obHumanReadable = false;
            cfg.SetFlag(UserFlag::BYTES);
        } else if (arg == "--delta") {
            cfg.obCumulative = false;
            cfg.SetFlag(UserFlag::UB_DELTA);
        } else if (arg == "--group-by") {
            if (i + 1 >= argc) {
                return MakeError("Invalid value for --group-by: missing value");
            }
            std::string gbArg = argv[++i];
            static const std::array<std::string, 3> validGroupBy = {"tid+frame", "frame", "raw"};
            if (std::find(validGroupBy.begin(), validGroupBy.end(), gbArg) == validGroupBy.end()) {
                return MakeError("Invalid value for --group-by: '" + gbArg + "' is not valid (valid: tid+frame, frame, raw)");
            }
            cfg.obGroupBy = gbArg;
            cfg.SetFlag(UserFlag::GROUP_BY);
        } else if (arg == "--top") {
            if (i + 1 >= argc) {
                return MakeError("Invalid value for --top: missing value");
            }
            std::string topArg = argv[++i];
            std::string error;
            int32_t topVal = 0;
            if (!ParseInt32Value(topArg, topVal, error) || topVal <= 0) {
                return MakeError("Invalid value for --top: " + error);
            }
            cfg.obTop = topVal;
            cfg.SetFlag(UserFlag::TOP);
        } else if (arg == "--show") {
            if (i + 1 >= argc) {
                return MakeError("Invalid value for --show: missing value");
            }
            std::string showArg = argv[++i];
            static const std::array<std::string, 13> validFields = {
                "current", "total", "allocs", "frees", "peak",
                "min_size", "max_size", "first_seen", "leaked", "avg_size",
                "churn", "churn_rate", "alloc_rate"
            };
            auto fields = ParseCsvList(showArg);
            for (const auto& f : fields) {
                if (std::find(validFields.begin(), validFields.end(), f) == validFields.end()) {
                    return MakeError("Invalid value for --show: '" + f + "' is not a valid field (valid: current, total, allocs, frees, peak, min_size, max_size, first_seen, leaked, avg_size, churn, churn_rate, alloc_rate)");
                }
            }
            cfg.obShowFields = showArg;
            cfg.SetFlag(UserFlag::SHOW_FIELDS);
        } else if (arg == "--strict-snapshot") {
            cfg.strictSnapshot = true;
            cfg.SetFlag(UserFlag::STRICT_SNAP);
        } else if (arg == "--csv") {
            // 可选值: --csv 无参数时使用默认路径, --csv <dir> 使用自定义路径
            cfg.SetFlag(UserFlag::CSV_OUTPUT);
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                cfg.csvOutputDir = argv[++i];
            }
            // 否则保持 CliConfig 默认值 (./results)
        } else if (arg == "--perflog") {
#ifdef SPDIAG_ENABLE_PERFLOG
            cfg.perflogShow = true;
            cfg.perflogStart = true;
#else
            result.error = "--perflog is not available (build with -s on)";
            return result;
#endif
        } else if (arg == "--help" || arg == "-h") {
            result.helpRequested = true;
            return result;
        } else {
            return MakeError("Unknown argument: " + arg);
        }
        ++i;
    }

    // Phase 3: cross-validation
    std::string validationError = ValidateCommandOptions(cfg);
    if (!validationError.empty()) {
        return MakeError(validationError);
    }

    return result;
}

CliConfig CliConfig::ParseArgs(int argc, char* argv[]) {
    auto result = ParseArgsInternal(argc, argv);
    if (result.helpRequested) {
        PrintUsage();
        std::exit(0);
    }
    if (result.versionRequested) {
        std::cout << "spdiag version " << SpDiag::versionString << std::endl;
        // 打印构建身份信息（commit SHA + 时间戳 + 架构），用于验证脚本
        // 判断 ctest 跑的是否是当前源码编译的 spdiag。
#ifdef SPDIAG_BUILD_GIT_SHA
        std::cout << "build: " << SPDIAG_BUILD_GIT_SHA
                  << " (" << SPDIAG_BUILD_TIMESTAMP << ", " << SPDIAG_BUILD_ARCH << ")" << std::endl;
#endif
        std::exit(0);
    }
    if (!result.error.empty()) {
        SPDIAG_LOG_ERROR(SpDiag::LogModule::CLI, "Command line parsing error: %s", result.error.c_str());
        PrintUsage();
        std::exit(1);
    }
    if (!result.commandSpecified) {
        std::cerr << "Warning: no command specified, defaulting to 'show'.\n"
                  << "Run 'spdiag --help' for available commands.\n\n";
    }
    return result.config;
}

CliConfig CliConfig::LoadFromFile(const std::string& path, bool silent) {
    CliConfig cfg;
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        if (!silent) {
            SPDIAG_LOG_ERROR(SpDiag::LogModule::CLI, "Cannot open config file: %s", path.c_str());
        }
        return cfg;
    }

    std::string section;
    std::string line;
    while (std::getline(ifs, line)) {
        auto commentPos = line.find_first_of("#;");
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }

        TrimInPlace(line);
        if (line.empty()) {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            section = ToLowerCopy(line.substr(1, line.size() - 2));
            TrimInPlace(section);
            continue;
        }

        auto eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = line.substr(0, eqPos);
        std::string val = line.substr(eqPos + 1);

        TrimInPlace(key);
        TrimInPlace(val);
        std::string lowerKey = ToLowerCopy(key);

        if (lowerKey == "shm_name" || (section == "shm" && lowerKey == "name")) {
            cfg.shmName = val;
        } else if (lowerKey == "version") {
            cfg.expectedVersion = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
        } else if (lowerKey == "data_dir") {
            cfg.dataDir = val;
        } else if (lowerKey == "history_size") {
            cfg.historySize = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
        } else if (lowerKey == "dump_interval_ms") {
            cfg.dumpIntervalMs = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
        } else if (lowerKey == "level_gate") {
            cfg.levelGate = static_cast<uint8_t>(std::strtoul(val.c_str(), nullptr, 10));
        } else if (lowerKey == "num_cores") {
            cfg.numCores = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
        } else if (lowerKey == "maxslots") {
            cfg.maxSlots = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
        } else if (lowerKey == "maxprograms") {
            cfg.maxPrograms = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
        }
    }

    return cfg;
}

}
}
