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
#include "src/cli/sort_spec.h"
#include "src/cli/glob_match.h"

using namespace SpDiag;
using namespace SpDiag::Cli;

TEST(CliConfigTest, DefaultValues) {
    CliConfig config;
    EXPECT_EQ(config.command, Command::SHOW);
    EXPECT_EQ(config.shmName, "/spdiag_shm_default");
    EXPECT_FALSE(config.showDetail);
    EXPECT_EQ(config.coreStart, -1);
    EXPECT_EQ(config.coreEnd, -1);
    EXPECT_EQ(config.levelFilter, 0);
    EXPECT_EQ(config.levelOp, LevelOp::LE);
    EXPECT_FALSE(config.hasLevelRange);
    EXPECT_TRUE(config.sortSpec.empty());
    EXPECT_TRUE(config.filterGlobs.empty());
    EXPECT_EQ(config.watchIntervalMs, 2000u);
    EXPECT_FALSE(config.strictSnapshot);
    EXPECT_EQ(config.obTargetPid, -1);
    EXPECT_EQ(config.obIntervalMs, 1000u);
    EXPECT_EQ(config.obGroupBy, "tid+frame");
    EXPECT_EQ(config.obTop, 20);
    EXPECT_EQ(config.historySize, 30u);
    EXPECT_EQ(config.dataDir, "/tmp/spdiag_data");
    EXPECT_EQ(config.maxSlots, 1024u);
    EXPECT_EQ(config.maxPrograms, 8u);
}

static CliConfig Parse(std::vector<std::string> args) {
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>("spdiag"));
    for (auto& a : args) {
        argv.push_back(const_cast<char*>(a.c_str()));
    }
    return CliConfig::ParseArgs(static_cast<int>(argv.size()), argv.data());
}

static ParseResult ParseInternal(std::vector<std::string> args) {
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>("spdiag"));
    for (auto& a : args) {
        argv.push_back(const_cast<char*>(a.c_str()));
    }
    return ParseArgsInternal(static_cast<int>(argv.size()), argv.data());
}

TEST(CliConfigTest, ParseShow) {
    auto config = Parse({"show"});
    EXPECT_EQ(config.command, Command::SHOW);
}

TEST(CliConfigTest, ParseWatch) {
    auto config = Parse({"watch", "--interval", "500"});
    EXPECT_EQ(config.command, Command::WATCH);
    EXPECT_EQ(config.watchIntervalMs, 500u);
}

TEST(CliConfigTest, ParseStart) {
    auto config = Parse({"start"});
    EXPECT_EQ(config.command, Command::START);
}

TEST(CliConfigTest, ParseStop) {
    auto config = Parse({"stop"});
    EXPECT_EQ(config.command, Command::STOP);
}

TEST(CliConfigTest, ParseRestart) {
    auto config = Parse({"restart"});
    EXPECT_EQ(config.command, Command::RESTART);
}

TEST(CliConfigTest, ParseStatus) {
    auto config = Parse({"status"});
    EXPECT_EQ(config.command, Command::STATUS);
}

TEST(CliConfigTest, ParseClear) {
    auto config = Parse({"clear"});
    EXPECT_EQ(config.command, Command::CLEAR);
}

TEST(CliConfigTest, ParseHistory) {
    auto config = Parse({"history"});
    EXPECT_EQ(config.command, Command::HISTORY);
}

#ifdef SPDIAG_ENABLE_OB_MEMORY
TEST(CliConfigTest, ParseMemstat) {
    auto config = Parse({"memstat", "--pid", "1234"});
    EXPECT_EQ(config.command, Command::MEMSTAT);
    EXPECT_EQ(config.obTargetPid, 1234);
}
#else
TEST(CliConfigErrorTest, MemstatUnavailableWhenDisabled) {
    auto result = ParseInternal({"memstat", "--pid", "1234"});
    EXPECT_NE(result.error.find("Unknown argument: memstat"), std::string::npos);
}
#endif

#ifdef SPDIAG_ENABLE_OB_CACHE
TEST(CliConfigTest, ParseCachestat) {
    auto config = Parse({"cachestat", "--pid", "5678"});
    EXPECT_EQ(config.command, Command::CACHESTAT);
    EXPECT_EQ(config.obTargetPid, 5678);
}
#else
TEST(CliConfigErrorTest, CachestatUnavailableWhenDisabled) {
    auto result = ParseInternal({"cachestat", "--pid", "5678"});
    EXPECT_NE(result.error.find("Unknown argument: cachestat"), std::string::npos);
}
#endif

TEST(CliConfigTest, ParseDetail) {
    auto config = Parse({"show", "-d"});
    EXPECT_TRUE(config.showDetail);
}

TEST(CliConfigTest, ParseCoreRange) {
    auto config = Parse({"show", "-c", "0-3"});
    EXPECT_EQ(config.coreStart, 0);
    EXPECT_EQ(config.coreEnd, 3);
}

TEST(CliConfigTest, ParseLevelFilter) {
    auto config = Parse({"show", "-l", "2"});
    EXPECT_EQ(config.levelFilter, 2);
    EXPECT_EQ(config.levelOp, LevelOp::LE);
}

TEST(CliConfigTest, ParseSortField) {
    auto config = Parse({"show", "-s", "ticks"});
    EXPECT_EQ(config.sortSpec, "ticks");
}

TEST(CliConfigTest, ParseSortMultiKey) {
    auto config = Parse({"show", "--sort", "total:desc,name:asc"});
    EXPECT_EQ(config.sortSpec, "total:desc,name:asc");
}

TEST(CliConfigTest, ParseFilter) {
    auto config = Parse({"show", "--filter", "Compute*,Memory*"});
    ASSERT_EQ(config.filterGlobs.size(), 2u);
    EXPECT_EQ(config.filterGlobs[0], "Compute*");
    EXPECT_EQ(config.filterGlobs[1], "Memory*");
}

#ifdef SPDIAG_ENABLE_OB_MEMORY
TEST(CliConfigTest, ParseObSort) {
    auto config = Parse({"memstat", "--pid", "1", "--sort", "leaked:desc,current"});
    EXPECT_EQ(config.sortSpec, "leaked:desc,current");
}

TEST(CliConfigTest, ParseObFilter) {
    auto config = Parse({"memstat", "--pid", "1", "--filter", "*Cache*,*alloc*"});
    ASSERT_EQ(config.filterGlobs.size(), 2u);
    EXPECT_EQ(config.filterGlobs[0], "*Cache*");
    EXPECT_EQ(config.filterGlobs[1], "*alloc*");
}
#endif

TEST(SortSpecTest, ParsePerfPointSingleKey) {
    std::string err;
    auto keys = ParseSortSpec("ticks", "perfpoint", err);
    ASSERT_TRUE(err.empty());
    ASSERT_EQ(keys.size(), 1u);
    EXPECT_EQ(keys[0].key, SortKey::TICKS);
    EXPECT_TRUE(keys[0].desc);  // default for ticks
}

TEST(SortSpecTest, ParsePerfPointMultiKey) {
    std::string err;
    auto keys = ParseSortSpec("total:desc,name:asc", "perfpoint", err);
    ASSERT_TRUE(err.empty());
    ASSERT_EQ(keys.size(), 2u);
    EXPECT_EQ(keys[0].key, SortKey::TOTAL_TIME);
    EXPECT_TRUE(keys[0].desc);
    EXPECT_EQ(keys[1].key, SortKey::NAME);
    EXPECT_FALSE(keys[1].desc);
}

TEST(SortSpecTest, ParsePerfPointInvalidKey) {
    std::string err;
    auto keys = ParseSortSpec("unknown", "perfpoint", err);
    EXPECT_FALSE(err.empty());
    EXPECT_TRUE(keys.empty());
}

TEST(SortSpecTest, ParseMemstatMultiKey) {
    std::string err;
    auto keys = ParseSortSpec("leaked:desc,avg_size:asc,frame", "memstat", err);
    ASSERT_TRUE(err.empty());
    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0].key, SortKey::LEAKED);
    EXPECT_TRUE(keys[0].desc);
    EXPECT_EQ(keys[1].key, SortKey::AVG_SIZE);
    EXPECT_FALSE(keys[1].desc);
    EXPECT_EQ(keys[2].key, SortKey::FRAME);
    EXPECT_FALSE(keys[2].desc);  // default for frame is asc
}

TEST(SortSpecTest, ParseInvalidDirection) {
    std::string err;
    auto keys = ParseSortSpec("ticks:up", "perfpoint", err);
    EXPECT_FALSE(err.empty());
    EXPECT_TRUE(keys.empty());
}

TEST(SortSpecTest, DefaultMemstatSortKeys) {
    const auto& keys = DefaultMemstatSortKeys();
    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0].key, SortKey::CURRENT);
    EXPECT_TRUE(keys[0].desc);
    EXPECT_EQ(keys[1].key, SortKey::PEAK);
    EXPECT_TRUE(keys[1].desc);
    EXPECT_EQ(keys[2].key, SortKey::ALLOCS);
    EXPECT_TRUE(keys[2].desc);
}

TEST(SortSpecTest, DefaultPerfPointSortKeys) {
    const auto& keys = DefaultPerfPointSortKeys();
    ASSERT_EQ(keys.size(), 1u);
    EXPECT_EQ(keys[0].key, SortKey::NAME);
    EXPECT_FALSE(keys[0].desc);
}

TEST(GlobMatchTest, ExactMatch) {
    EXPECT_TRUE(GlobMatch("hello", "hello"));
    EXPECT_FALSE(GlobMatch("hello", "world"));
}

TEST(GlobMatchTest, StarWildcard) {
    EXPECT_TRUE(GlobMatch("Compute*", "ComputeStage"));
    EXPECT_TRUE(GlobMatch("*", "anything"));
    EXPECT_TRUE(GlobMatch("Comp*Stage", "ComputeStage"));
    EXPECT_FALSE(GlobMatch("Comp*Stage", "ComputePhase"));
}

TEST(GlobMatchTest, QuestionWildcard) {
    EXPECT_TRUE(GlobMatch("Compute?", "Compute1"));
    EXPECT_FALSE(GlobMatch("Compute?", "Compute12"));
}

TEST(GlobMatchTest, MultiplePatterns) {
    EXPECT_TRUE(GlobMatch("Module.Point", "Module.Point"));
    EXPECT_TRUE(GlobMatch("Module.*", "Module.Point"));
    EXPECT_TRUE(GlobMatch("*.Point", "Module.Point"));
    EXPECT_FALSE(GlobMatch("Other.*", "Module.Point"));
}

// Backslash escape makes the next pattern character literal (glob_match.h:25-26).
TEST(GlobMatchTest, BackslashEscapesStar) {
    // Pattern literal "a\*b": the escaped '*' must match a literal '*', not act as a wildcard.
    EXPECT_TRUE(GlobMatch("a\\*b", "a*b"));
    EXPECT_FALSE(GlobMatch("a\\*b", "axxb"));
    EXPECT_FALSE(GlobMatch("a\\*b", "aXb"));
}

TEST(GlobMatchTest, BackslashEscapesQuestion) {
    // Pattern literal "a\?b": the escaped '?' must match a literal '?', not a single wildcard char.
    EXPECT_TRUE(GlobMatch("a\\?b", "a?b"));
    EXPECT_FALSE(GlobMatch("a\\?b", "axb"));
}

TEST(GlobMatchTest, BackslashEscapesBackslash) {
    // Pattern literal "a\\b" (source "a\\\\b"): escaped backslash matches one literal '\'.
    EXPECT_TRUE(GlobMatch("a\\\\b", "a\\b"));
    EXPECT_FALSE(GlobMatch("a\\\\b", "ab"));
}

TEST(GlobMatchTest, EscapeThenWildcard) {
    // Pattern "\**" = literal '*' followed by wildcard '*': matches any string starting with '*'.
    EXPECT_TRUE(GlobMatch("\\**", "*abc"));
    EXPECT_TRUE(GlobMatch("\\**", "*"));
    EXPECT_FALSE(GlobMatch("\\**", "abc"));
}

TEST(GlobMatchTest, EscapeMismatchFallsBack) {
    // "\x" matches literal 'x'; against 'a' there is no star to backtrack to -> no match.
    EXPECT_TRUE(GlobMatch("\\xa", "xa"));
    EXPECT_FALSE(GlobMatch("\\x", "a"));
}

TEST(GlobMatchTest, TrailingLoneBackslash) {
    // A trailing '\' with no char after it cannot complete an escape against a shorter string.
    EXPECT_FALSE(GlobMatch("abc\\", "abc"));
}

TEST(GlobMatchTest, EmptyPatternAndString) {
    EXPECT_TRUE(GlobMatch("", ""));
    EXPECT_FALSE(GlobMatch("", "a"));
    EXPECT_FALSE(GlobMatch("a", ""));
}

TEST(GlobMatchTest, AllStarsMatchAny) {
    EXPECT_TRUE(GlobMatch("*", ""));
    EXPECT_TRUE(GlobMatch("***", "abc"));
    EXPECT_TRUE(GlobMatch("*", "anything"));
}

TEST(GlobMatchTest, QuestionRequiresExactlyOneChar) {
    EXPECT_FALSE(GlobMatch("?", ""));
    EXPECT_TRUE(GlobMatch("?", "x"));
    EXPECT_FALSE(GlobMatch("?", "xy"));
}

TEST(CliConfigTest, ParseStrictSnapshot) {
    auto config = Parse({"show", "--strict-snapshot"});
    EXPECT_TRUE(config.strictSnapshot);
}

#ifdef SPDIAG_ENABLE_OB_MEMORY
TEST(CliConfigTest, ParseLibcPath) {
    auto config = Parse({"memstat", "--pid", "1", "--libc-path", "/lib/x86_64-linux-gnu/libc.so.6"});
    EXPECT_EQ(config.obLibcPath, "/lib/x86_64-linux-gnu/libc.so.6");
}
#endif

#ifdef SPDIAG_ENABLE_OB_CACHE
TEST(CliConfigTest, ParseCacheLevels) {
    auto config = Parse({"cachestat", "--pid", "1", "--cache-levels", "L1-dcache,LLC"});
    ASSERT_EQ(config.obCacheLevels.size(), 2u);
    EXPECT_EQ(config.obCacheLevels[0], "L1-dcache");
    EXPECT_EQ(config.obCacheLevels[1], "LLC");
}

TEST(CliConfigTest, DefaultCacheLevels) {
    auto config = Parse({"cachestat", "--pid", "1"});
    ASSERT_EQ(config.obCacheLevels.size(), 3u);
    EXPECT_EQ(config.obCacheLevels[0], "L1-dcache");
    EXPECT_EQ(config.obCacheLevels[1], "L1-icache");
    EXPECT_EQ(config.obCacheLevels[2], "LLC");
}
#endif
// ===== Multiple commands error =====

TEST(CliConfigErrorTest, MultipleCommandsShowHistory) {
    auto r = ParseInternal({"show", "history"});
    EXPECT_FALSE(r.error.empty());
    EXPECT_NE(r.error.find("Multiple commands"), std::string::npos);
}

TEST(CliConfigErrorTest, MultipleCommandsStartStop) {
    auto r = ParseInternal({"start", "stop"});
    EXPECT_FALSE(r.error.empty());
    EXPECT_NE(r.error.find("Multiple commands"), std::string::npos);
}

TEST(CliConfigErrorTest, MultipleCommandsWatchShow) {
    auto r = ParseInternal({"watch", "show"});
    EXPECT_FALSE(r.error.empty());
    EXPECT_NE(r.error.find("Multiple commands"), std::string::npos);
}

// ===== Parameter-command cross-validation =====

TEST(CliConfigErrorTest, StartWithShowOptions) {
    auto r = ParseInternal({"start", "-d", "-c", "0-3", "-l", "2"});
    EXPECT_FALSE(r.error.empty());
    EXPECT_NE(r.error.find("not applicable"), std::string::npos);
}

TEST(CliConfigErrorTest, ClearWithDetailOption) {
    auto r = ParseInternal({"clear", "-d"});
    EXPECT_FALSE(r.error.empty());
    EXPECT_NE(r.error.find("not applicable"), std::string::npos);
}

TEST(CliConfigErrorTest, ShowWithPidOption) {
    auto r = ParseInternal({"show", "--pid", "1234"});
    EXPECT_FALSE(r.error.empty());
    EXPECT_NE(r.error.find("not applicable"), std::string::npos);
}

TEST(CliConfigErrorTest, ShowWithHistoryIndex) {
    auto r = ParseInternal({"show", "-i", "5"});
    EXPECT_FALSE(r.error.empty());
    EXPECT_NE(r.error.find("not applicable"), std::string::npos);
}

TEST(CliConfigErrorTest, ShowWithHistoryN) {
    auto r = ParseInternal({"show", "-n", "5"});
    EXPECT_FALSE(r.error.empty());
    EXPECT_NE(r.error.find("not applicable"), std::string::npos);
}

#ifdef SPDIAG_ENABLE_OB_MEMORY
TEST(CliConfigErrorTest, MemstatWithShowOptions) {
    auto r = ParseInternal({"memstat", "--pid", "1", "-d", "-c", "0-3"});
    EXPECT_FALSE(r.error.empty());
    EXPECT_NE(r.error.find("not applicable"), std::string::npos);
}
#endif

TEST(CliConfigErrorTest, HistoryWithShowOptions) {
    auto r = ParseInternal({"history", "-d", "-l", "2", "-s", "ticks"});
    EXPECT_FALSE(r.error.empty());
    EXPECT_NE(r.error.find("not applicable"), std::string::npos);
}

// ===== OB parameter value validation =====

#ifdef SPDIAG_ENABLE_OB_MEMORY
TEST(CliConfigErrorTest, GroupByInvalidValue) {
    auto r = ParseInternal({"memstat", "--pid", "1", "--group-by", "foobar"});
    EXPECT_FALSE(r.error.empty());
    EXPECT_NE(r.error.find("--group-by"), std::string::npos);
}

TEST(CliConfigErrorTest, GroupByMissingValue) {
    auto r = ParseInternal({"memstat", "--pid", "1", "--group-by"});
    EXPECT_FALSE(r.error.empty());
    EXPECT_NE(r.error.find("missing value"), std::string::npos);
}

TEST(CliConfigErrorTest, TopInvalidValue) {
    auto r = ParseInternal({"memstat", "--pid", "1", "--top", "abc"});
    EXPECT_FALSE(r.error.empty());
    EXPECT_NE(r.error.find("--top"), std::string::npos);
}

TEST(CliConfigErrorTest, TopMissingValue) {
    auto r = ParseInternal({"memstat", "--pid", "1", "--top"});
    EXPECT_FALSE(r.error.empty());
    EXPECT_NE(r.error.find("missing value"), std::string::npos);
}

TEST(CliConfigErrorTest, ShowFieldsInvalid) {
    auto r = ParseInternal({"memstat", "--pid", "1", "--show", "invalid_field"});
    EXPECT_FALSE(r.error.empty());
    EXPECT_NE(r.error.find("--show"), std::string::npos);
}
#endif

#ifdef SPDIAG_ENABLE_OB_CACHE
TEST(CliConfigErrorTest, CacheLevelsInvalid) {
    auto r = ParseInternal({"cachestat", "--pid", "1", "--cache-levels", "FOO,BAR"});
    EXPECT_FALSE(r.error.empty());
    EXPECT_NE(r.error.find("--cache-levels"), std::string::npos);
}

TEST(CliConfigTest, CacheLevelsAllValid) {
    auto config = Parse({"cachestat", "--pid", "1", "--cache-levels", "L1-dcache,L1-icache,LLC,dTLB,iTLB,branch"});
    ASSERT_EQ(config.obCacheLevels.size(), 6u);
    EXPECT_EQ(config.obCacheLevels[0], "L1-dcache");
    EXPECT_EQ(config.obCacheLevels[1], "L1-icache");
    EXPECT_EQ(config.obCacheLevels[2], "LLC");
    EXPECT_EQ(config.obCacheLevels[3], "dTLB");
    EXPECT_EQ(config.obCacheLevels[4], "iTLB");
    EXPECT_EQ(config.obCacheLevels[5], "branch");
}
#endif

// ===== Core range validation =====

TEST(CliConfigErrorTest, CoreNegativeValue) {
    auto r = ParseInternal({"show", "-c", "-1"});
    EXPECT_FALSE(r.error.empty());
    EXPECT_NE(r.error.find("--core"), std::string::npos);
}

TEST(CliConfigErrorTest, CoreRangeInverted) {
    auto r = ParseInternal({"show", "-c", "5-3"});
    EXPECT_FALSE(r.error.empty());
    EXPECT_NE(r.error.find("range start must be <= end"), std::string::npos);
}

// ===== Unknown argument =====

TEST(CliConfigErrorTest, UnknownArgument) {
    auto r = ParseInternal({"show", "--foobar"});
    EXPECT_FALSE(r.error.empty());
    EXPECT_NE(r.error.find("Unknown argument"), std::string::npos);
}

// ===== Valid OB parameter values =====

#ifdef SPDIAG_ENABLE_OB_MEMORY
TEST(CliConfigTest, ValidGroupByValues) {
    auto c1 = Parse({"memstat", "--pid", "1", "--group-by", "frame"});
    EXPECT_EQ(c1.obGroupBy, "frame");
    auto c2 = Parse({"memstat", "--pid", "1", "--group-by", "raw"});
    EXPECT_EQ(c2.obGroupBy, "raw");
}

TEST(CliConfigTest, ValidTopValue) {
    auto config = Parse({"memstat", "--pid", "1", "--top", "50"});
    EXPECT_EQ(config.obTop, 50);
}

TEST(CliConfigTest, ValidShowFields) {
    auto config = Parse({"memstat", "--pid", "1", "--show", "min_size,leaked"});
    EXPECT_EQ(config.obShowFields, "min_size,leaked");
}
#endif

#ifdef SPDIAG_ENABLE_OB_CACHE
TEST(CliConfigTest, ValidCacheLevels) {
    auto config = Parse({"cachestat", "--pid", "1", "--cache-levels", "L1-icache,LLC"});
    ASSERT_EQ(config.obCacheLevels.size(), 2u);
    EXPECT_EQ(config.obCacheLevels[0], "L1-icache");
    EXPECT_EQ(config.obCacheLevels[1], "LLC");
}
#endif

// ===== Clear-history flag =====

TEST(CliConfigTest, ClearHistoryFlag) {
    auto config = Parse({"history", "-H"});
    EXPECT_TRUE(config.clearHistory);
    auto config2 = Parse({"history", "--clear-history"});
    EXPECT_TRUE(config2.clearHistory);
}
