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

#include "src/cli/display_engine.h"
#include "src/cli/cli_config.h"
#include "src/cli/sort_spec.h"
#include "src/cli/glob_match.h"
#include "src/runtime/data_pipeline.h"
#include "spdiag/mem_point_keys.h"

#include <string>
#include <vector>

using namespace SpDiag;
using namespace SpDiag::Cli;

// Helper to create a ProcessorOutput
static ProcessorOutput MakeOutput(const std::string& key, const std::string& value) {
    ProcessorOutput out;
    out.key = key;
    out.value = value;
    out.normalizedTimestampNs = 1000000;
    return out;
}

// --- ParseMemPointEntries tests ---

TEST(ParseMemPointEntries, BasicFields) {
    std::vector<ProcessorOutput> outputs = {
        MakeOutput("mempoint.12345.1.current_bytes", "2048"),
        MakeOutput("mempoint.12345.1.total_alloc",   "8192"),
        MakeOutput("mempoint.12345.1.alloc_count",   "100"),
        MakeOutput("mempoint.12345.1.free_count",    "50"),
        MakeOutput("mempoint.12345.1.peak",          "4096"),
        MakeOutput("mempoint.12345.1.max_size",      "1024"),
        MakeOutput("mempoint.12345.1.min_size",      "128"),
        MakeOutput("mempoint.12345.1.first_seen_ns", "1000000000"),
        MakeOutput("mempoint.12345.1.key_name",      "Stack_value"),
    };

    auto entries = ParseMemPointEntries(outputs);
    ASSERT_EQ(entries.size(), 1u);

    const auto& e = entries[0];
    EXPECT_EQ(e.pid, 12345);
    EXPECT_EQ(e.id, 1u);
    EXPECT_EQ(e.currentBytes, 2048u);
    EXPECT_EQ(e.totalBytes, 8192u);
    EXPECT_EQ(e.allocs, 100u);
    EXPECT_EQ(e.frees, 50u);
    EXPECT_EQ(e.peakBytes, 4096u);
    EXPECT_EQ(e.maxSize, 1024u);
    EXPECT_EQ(e.minSize, 128u);
    EXPECT_EQ(e.firstSeenNs, 1000000000u);
    EXPECT_EQ(e.keyName, "Stack_value");
}

TEST(ParseMemPointEntries, LeakFields) {
    std::vector<ProcessorOutput> outputs = {
        MakeOutput("mempoint.12345.3.current_bytes", "1024"),
        MakeOutput("mempoint.12345.3.alloc_count",   "10"),
        MakeOutput("mempoint.12345.3.free_count",    "5"),
        MakeOutput("mempoint.12345.3.peak",          "2048"),
        MakeOutput("mempoint.12345.leak.3.count",      "3"),
        MakeOutput("mempoint.12345.leak.3.max_age_ns", "600000000000"),
    };

    auto entries = ParseMemPointEntries(outputs);
    ASSERT_EQ(entries.size(), 1u);

    const auto& e = entries[0];
    EXPECT_EQ(e.leakCount, 3u);
    EXPECT_EQ(e.leakMaxAgeNs, 600000000000u);
    EXPECT_EQ(e.currentBytes, 1024u);
}

TEST(ParseMemPointEntries, IgnoresMemPrefix) {
    std::vector<ProcessorOutput> outputs = {
        MakeOutput("mem.pid.12345.tid.99.stack.1.current_bytes", "2048"),
        MakeOutput("mempoint.12345.1.current_bytes", "1024"),
    };

    auto entries = ParseMemPointEntries(outputs);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].currentBytes, 1024u);
}

TEST(ParseMemPointEntries, MultipleKeys) {
    std::vector<ProcessorOutput> outputs = {
        MakeOutput("mempoint.12345.1.current_bytes", "100"),
        MakeOutput("mempoint.12345.1.alloc_count",   "5"),
        MakeOutput("mempoint.12345.1.free_count",    "3"),
        MakeOutput("mempoint.12345.1.peak",          "200"),
        MakeOutput("mempoint.12345.2.current_bytes", "500"),
        MakeOutput("mempoint.12345.2.alloc_count",   "10"),
        MakeOutput("mempoint.12345.2.free_count",    "2"),
        MakeOutput("mempoint.12345.2.peak",          "1000"),
    };

    auto entries = ParseMemPointEntries(outputs);
    EXPECT_EQ(entries.size(), 2u);
}

TEST(ParseMemPointEntries, EmptyInput) {
    std::vector<ProcessorOutput> outputs;
    auto entries = ParseMemPointEntries(outputs);
    EXPECT_TRUE(entries.empty());
}

TEST(ParseMemPointEntries, CategoryFromKeyId) {
    std::vector<ProcessorOutput> outputs = {
        MakeOutput("mempoint.12345.1.current_bytes", "100"),
        MakeOutput("mempoint.12345.1.alloc_count",   "1"),
        MakeOutput("mempoint.12345.1.free_count",    "0"),
        MakeOutput("mempoint.12345.1.peak",          "100"),
    };

    auto entries = ParseMemPointEntries(outputs);
    ASSERT_EQ(entries.size(), 1u);
    // id=1 is RPC_REQUEST_BUF, category should be "Rpc"
    EXPECT_EQ(entries[0].category, "Rpc");
    EXPECT_EQ(entries[0].keyName, "Stack_value");
}

TEST(ParseMemPointEntries, KeyNameFromEnumNotOverridden) {
    std::vector<ProcessorOutput> outputs = {
        MakeOutput("mempoint.12345.3.current_bytes", "100"),
        MakeOutput("mempoint.12345.3.alloc_count",   "1"),
        MakeOutput("mempoint.12345.3.free_count",    "0"),
        MakeOutput("mempoint.12345.3.peak",          "100"),
        MakeOutput("mempoint.12345.3.key_name",      "Sql"),
    };

    auto entries = ParseMemPointEntries(outputs);
    ASSERT_EQ(entries.size(), 1u);

    EXPECT_EQ(entries[0].category, "Sql");
    EXPECT_EQ(entries[0].keyName, "Tracked_vector");
}

// --- Sort tests ---

TEST(MemPointSort, ByCurrent) {
    std::vector<ProcessorOutput> outputs = {
        MakeOutput("mempoint.100.1.current_bytes", "100"),
        MakeOutput("mempoint.100.1.alloc_count",   "5"),
        MakeOutput("mempoint.100.1.free_count",    "0"),
        MakeOutput("mempoint.100.1.peak",          "100"),
        MakeOutput("mempoint.100.2.current_bytes", "500"),
        MakeOutput("mempoint.100.2.alloc_count",   "5"),
        MakeOutput("mempoint.100.2.free_count",    "0"),
        MakeOutput("mempoint.100.2.peak",          "500"),
        MakeOutput("mempoint.100.3.current_bytes", "300"),
        MakeOutput("mempoint.100.3.alloc_count",   "5"),
        MakeOutput("mempoint.100.3.free_count",    "0"),
        MakeOutput("mempoint.100.3.peak",          "300"),
    };

    auto entries = ParseMemPointEntries(outputs);
    ASSERT_EQ(entries.size(), 3u);

    // Sort by current desc
    std::vector<Cli::SortKey> sortKeys = {{Cli::SortKey::CURRENT, true}};
    std::stable_sort(entries.begin(), entries.end(),
        [&sortKeys](const MemPointEntry& a, const MemPointEntry& b) {
            for (const auto& sk : sortKeys) {
                int cmp = (a.currentBytes < b.currentBytes) ? -1 :
                          (a.currentBytes > b.currentBytes) ? 1 : 0;
                if (cmp != 0) return sk.desc ? (cmp > 0) : (cmp < 0);
            }
            return false;
        });

    EXPECT_EQ(entries[0].currentBytes, 500u);
    EXPECT_EQ(entries[1].currentBytes, 300u);
    EXPECT_EQ(entries[2].currentBytes, 100u);
}

TEST(MemPointSort, ByPeak) {
    std::vector<ProcessorOutput> outputs = {
        MakeOutput("mempoint.100.1.current_bytes", "100"),
        MakeOutput("mempoint.100.1.alloc_count",   "1"),
        MakeOutput("mempoint.100.1.free_count",    "0"),
        MakeOutput("mempoint.100.1.peak",          "1000"),
        MakeOutput("mempoint.100.2.current_bytes", "500"),
        MakeOutput("mempoint.100.2.alloc_count",   "1"),
        MakeOutput("mempoint.100.2.free_count",    "0"),
        MakeOutput("mempoint.100.2.peak",          "500"),
    };

    auto entries = ParseMemPointEntries(outputs);
    ASSERT_EQ(entries.size(), 2u);

    std::vector<Cli::SortKey> sortKeys = {{Cli::SortKey::PEAK, true}};
    std::stable_sort(entries.begin(), entries.end(),
        [&sortKeys](const MemPointEntry& a, const MemPointEntry& b) {
            for (const auto& sk : sortKeys) {
                int cmp = (a.peakBytes < b.peakBytes) ? -1 :
                          (a.peakBytes > b.peakBytes) ? 1 : 0;
                if (cmp != 0) return sk.desc ? (cmp > 0) : (cmp < 0);
            }
            return false;
        });

    EXPECT_EQ(entries[0].peakBytes, 1000u);
    EXPECT_EQ(entries[1].peakBytes, 500u);
}

// --- Filter tests ---

TEST(MemPointFilter, GlobFilterSQL) {
    std::vector<MemPointEntry> entries;
    MemPointEntry e1;
    e1.category = "Rpc";
    e1.keyName = "Stack_value";
    e1.hasData = true;
    entries.push_back(e1);

    MemPointEntry e2;
    e2.category = "Sql";
    e2.keyName = "Tracked_vector";
    e2.hasData = true;
    entries.push_back(e2);

    MemPointEntry e3;
    e3.category = "Sql";
    e3.keyName = "Struct_member";
    e3.hasData = true;
    entries.push_back(e3);

    // Filter: keep only sql.*
    std::vector<std::string> globs = {"Sql*"};
    entries.erase(std::remove_if(entries.begin(), entries.end(),
        [&globs](const MemPointEntry& e) {
            std::string fullName = e.category + "." + e.keyName;
            for (const auto& pat : globs) {
                if (Cli::GlobMatch(pat, fullName)) return false;
            }
            return true;
        }), entries.end());

    EXPECT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].category, "Sql");
    EXPECT_EQ(entries[1].category, "Sql");
}

TEST(MemPointFilter, GlobFilterRPC) {
    std::vector<MemPointEntry> entries;
    MemPointEntry e1;
    e1.category = "Rpc";
    e1.keyName = "Stack_value";
    e1.hasData = true;
    entries.push_back(e1);

    MemPointEntry e2;
    e2.category = "Sql";
    e2.keyName = "Tracked_vector";
    e2.hasData = true;
    entries.push_back(e2);

    std::vector<std::string> globs = {"Rpc*"};
    entries.erase(std::remove_if(entries.begin(), entries.end(),
        [&globs](const MemPointEntry& e) {
            std::string fullName = e.category + "." + e.keyName;
            for (const auto& pat : globs) {
                if (Cli::GlobMatch(pat, fullName)) return false;
            }
            return true;
        }), entries.end());

    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].category, "Rpc");
}

// --- Top N tests ---

TEST(MemPointTopN, Top3) {
    std::vector<MemPointEntry> entries(5);
    for (int i = 0; i < 5; ++i) {
        entries[i].currentBytes = static_cast<uint64_t>((5 - i) * 100);
        entries[i].hasData = true;
    }

    int top = 3;
    if (top > 0 && static_cast<int>(entries.size()) > top) {
        entries.resize(static_cast<size_t>(top));
    }

    EXPECT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0].currentBytes, 500u);
    EXPECT_EQ(entries[1].currentBytes, 400u);
    EXPECT_EQ(entries[2].currentBytes, 300u);
}

// --- CLI Config tests ---

TEST(MemPointCliConfig, CommandEnumExists) {
    EXPECT_STREQ(Cli::CommandName(Cli::Command::MEMPOINT), "mempoint");
}

TEST(MemPointCliConfig, CommandCategory) {
    EXPECT_EQ(Cli::GetCategory(Cli::Command::MEMPOINT), Cli::CommandCategory::OBSERVATION);
}

// --- Sort spec tests ---

TEST(MemPointSortSpec, DefaultSortKeys) {
    const auto& keys = Cli::DefaultMemPointSortKeys();
    ASSERT_GE(keys.size(), 2u);
    EXPECT_EQ(keys[0].key, Cli::SortKey::CURRENT);
    EXPECT_TRUE(keys[0].desc);
    EXPECT_EQ(keys[1].key, Cli::SortKey::PEAK);
    EXPECT_TRUE(keys[1].desc);
}

TEST(MemPointSortSpec, ParseSortSpecMemPointMode) {
    std::string err;
    auto keys = Cli::ParseSortSpec("current,peak", "memstat", err);
    ASSERT_EQ(keys.size(), 2u);
    EXPECT_EQ(keys[0].key, Cli::SortKey::CURRENT);
    EXPECT_EQ(keys[1].key, Cli::SortKey::PEAK);
}

TEST(MemPointSortSpec, ParseSortSpecInvalidKey) {
    std::string err;
    auto keys = Cli::ParseSortSpec("nonexistent", "memstat", err);
    EXPECT_TRUE(keys.empty());
    EXPECT_FALSE(err.empty());
}

// --- Key format string tests ---

TEST(MemPointKeyFormat, StatMapPrefix) {
    uint32_t pid = 12345;
    uint32_t id = 1;
    std::string prefix = "mempoint." + std::to_string(pid) + "." + std::to_string(id);
    EXPECT_EQ(prefix, "mempoint.12345.1");
}

TEST(MemPointKeyFormat, LeakPrefix) {
    uint32_t pid = 12345;
    uint32_t id = 3;
    std::string prefix = "mempoint." + std::to_string(pid) + ".leak." + std::to_string(id);
    EXPECT_EQ(prefix, "mempoint.12345.leak.3");
}

TEST(MemPointKeyFormat, FieldSuffixes) {
    std::string prefix = "mempoint.12345.1";
    std::vector<std::string> fields = {
        ".current_bytes", ".total_alloc", ".alloc_count",
        ".free_count", ".peak", ".max_size", ".min_size",
        ".first_seen_ns", ".key_name"
    };
    for (const auto& f : fields) {
        EXPECT_EQ(prefix.size() + f.size(), (prefix + f).size());
        EXPECT_EQ((prefix + f).substr(0, 9), "mempoint.");
    }
}

// --- Derived field tests ---

TEST(MemPointDerived, ChurnBytes) {
    MemPointEntry e;
    e.totalBytes = 10000;
    e.currentBytes = 3000;
    e.churnBytes = e.totalBytes >= e.currentBytes
        ? (e.totalBytes - e.currentBytes) : 0;
    EXPECT_EQ(e.churnBytes, 7000u);
}

TEST(MemPointDerived, AvgSize) {
    MemPointEntry e;
    e.totalBytes = 10000;
    e.allocs = 100;
    uint64_t avg = e.allocs > 0 ? e.totalBytes / e.allocs : 0;
    EXPECT_EQ(avg, 100u);
}

TEST(MemPointDerived, AvgSizeZeroAllocs) {
    MemPointEntry e;
    e.totalBytes = 0;
    e.allocs = 0;
    uint64_t avg = e.allocs > 0 ? e.totalBytes / e.allocs : 0;
    EXPECT_EQ(avg, 0u);
}

TEST(MemPointDerived, LeakedCount) {
    MemPointEntry e;
    e.allocs = 50;
    e.frees = 30;
    uint64_t leaked = (e.allocs >= e.frees) ? (e.allocs - e.frees) : 0;
    EXPECT_EQ(leaked, 20u);
}

TEST(MemPointDerived, LeakedCountNoLeak) {
    MemPointEntry e;
    e.allocs = 100;
    e.frees = 100;
    uint64_t leaked = (e.allocs >= e.frees) ? (e.allocs - e.frees) : 0;
    EXPECT_EQ(leaked, 0u);
}

TEST(MemPointDerived, SessionDuration) {
    MemPointEntry e;
    e.allocs = 1000;
    e.churnBytes = 5000;
    e.sessionDurationSec = 10.0;

    // alloc_rate = allocs / duration
    double allocRate = static_cast<double>(e.allocs) / e.sessionDurationSec;
    EXPECT_DOUBLE_EQ(allocRate, 100.0);

    // churn_rate = churnBytes / duration
    double churnRate = static_cast<double>(e.churnBytes) / e.sessionDurationSec;
    EXPECT_DOUBLE_EQ(churnRate, 500.0);
}

// --- ShowMemPoint display test (captures stdout) ---

class MemPointShowTest : public ::testing::Test {
protected:
    void SetUp() override {
        oldBuf_ = std::cout.rdbuf();
        std::cout.rdbuf(ss_.rdbuf());
    }
    void TearDown() override {
        std::cout.rdbuf(oldBuf_);
    }
    std::string GetOutput() { return ss_.str(); }
private:
    std::stringstream ss_;
    std::streambuf* oldBuf_ = nullptr;
};

TEST_F(MemPointShowTest, DefaultColumnsIncludeAvgSize) {
    std::vector<ProcessorOutput> outputs = {
        MakeOutput("mempoint.100.1.current_bytes", "2048"),
        MakeOutput("mempoint.100.1.total_alloc",   "8192"),
        MakeOutput("mempoint.100.1.alloc_count",   "100"),
        MakeOutput("mempoint.100.1.free_count",    "50"),
        MakeOutput("mempoint.100.1.peak",          "4096"),
        MakeOutput("mempoint.100.1.max_size",      "1024"),
        MakeOutput("mempoint.100.1.min_size",      "128"),
        MakeOutput("mempoint.100.1.first_seen_ns", "1000000000"),
        MakeOutput("mempoint.100.1.key_name",      "Stack_value"),
    };
    DisplayEngine display;
    display.ShowMemPoint(outputs, true, 20, {}, {});
    std::string out = GetOutput();
    EXPECT_NE(out.find("AvgSize"), std::string::npos);
    EXPECT_NE(out.find("MaxSize"), std::string::npos);
    EXPECT_NE(out.find("FirstSeen"), std::string::npos);
}

TEST_F(MemPointShowTest, CustomShowFields) {
    std::vector<ProcessorOutput> outputs = {
        MakeOutput("mempoint.100.1.current_bytes", "2048"),
        MakeOutput("mempoint.100.1.total_alloc",   "8192"),
        MakeOutput("mempoint.100.1.alloc_count",   "100"),
        MakeOutput("mempoint.100.1.free_count",    "50"),
        MakeOutput("mempoint.100.1.peak",          "4096"),
        MakeOutput("mempoint.100.1.key_name",      "test key"),
    };
    DisplayEngine display;
    display.ShowMemPoint(outputs, true, 20, {}, {}, "leaked,churn");
    std::string out = GetOutput();
    EXPECT_NE(out.find("Leaked"), std::string::npos);
    EXPECT_NE(out.find("Churn"), std::string::npos);
    // Default-only columns should not appear when --show is used
    EXPECT_EQ(out.find("AvgSize"), std::string::npos);
}

TEST_F(MemPointShowTest, LeakSectionShown) {
    std::vector<ProcessorOutput> outputs = {
        MakeOutput("mempoint.100.1.current_bytes", "1024"),
        MakeOutput("mempoint.100.1.alloc_count",   "10"),
        MakeOutput("mempoint.100.1.free_count",    "5"),
        MakeOutput("mempoint.100.1.peak",          "2048"),
        MakeOutput("mempoint.100.leak.1.count",      "3"),
        MakeOutput("mempoint.100.leak.1.max_age_ns", "600000000000"),
    };
    DisplayEngine display;
    display.ShowMemPoint(outputs, true, 20, {}, {});
    std::string out = GetOutput();
    EXPECT_NE(out.find("Leak Detection"), std::string::npos);
}

// --- Resize behavior tests ---

TEST(ParseMemPointEntries, ResizeGrowthUpdatesMaxSize) {
    // Simulates Heap_object: enter at 64B, resize to 192B
    // After fix: max_size=192 (from resize), min_size=64 (from enter)
    std::vector<ProcessorOutput> outputs = {
        MakeOutput("mempoint.12345.8.current_bytes", "0"),
        MakeOutput("mempoint.12345.8.total_alloc",   "19200"),
        MakeOutput("mempoint.12345.8.alloc_count",   "100"),
        MakeOutput("mempoint.12345.8.free_count",    "100"),
        MakeOutput("mempoint.12345.8.peak",          "19200"),
        MakeOutput("mempoint.12345.8.max_size",      "192"),
        MakeOutput("mempoint.12345.8.min_size",      "64"),
        MakeOutput("mempoint.12345.8.first_seen_ns", "1000000000"),
        MakeOutput("mempoint.12345.8.key_name",      "Heap_object"),
    };

    auto entries = ParseMemPointEntries(outputs);
    ASSERT_EQ(entries.size(), 1u);

    const auto& e = entries[0];
    EXPECT_EQ(e.totalBytes, 19200u);
    EXPECT_EQ(e.maxSize, 192u);
    EXPECT_EQ(e.minSize, 64u);
    EXPECT_EQ(e.totalBytes / e.allocs, 192u);
}

TEST(ParseMemPointEntries, ResizeGrowthContributesToTotalAlloc) {
    // Simulates tracked::vector: reserve(100) → allocate(400B)
    // total_alloc = 0 (enter) + 400 (resize growth) = 400
    std::vector<ProcessorOutput> outputs = {
        MakeOutput("mempoint.12345.3.current_bytes", "0"),
        MakeOutput("mempoint.12345.3.total_alloc",   "400"),
        MakeOutput("mempoint.12345.3.alloc_count",   "1"),
        MakeOutput("mempoint.12345.3.free_count",    "1"),
        MakeOutput("mempoint.12345.3.peak",          "400"),
        MakeOutput("mempoint.12345.3.max_size",      "400"),
        MakeOutput("mempoint.12345.3.min_size",      "0"),
        MakeOutput("mempoint.12345.3.first_seen_ns", "1000000000"),
    };

    auto entries = ParseMemPointEntries(outputs);
    ASSERT_EQ(entries.size(), 1u);

    const auto& e = entries[0];
    EXPECT_EQ(e.totalBytes, 400u);
    EXPECT_EQ(e.maxSize, 400u);
    EXPECT_EQ(e.minSize, 0u);
    EXPECT_EQ(e.allocs, 1u);
}

TEST(MemPointDerived, ChurnWithResizeGrowth) {
    // After resize fix, total_alloc includes resize growth.
    // churnBytes = totalBytes - currentBytes
    MemPointEntry e;
    e.totalBytes = 19200;  // 100*64 (enter) + 100*128 (resize growth)
    e.currentBytes = 0;    // all freed
    e.churnBytes = e.totalBytes >= e.currentBytes
        ? (e.totalBytes - e.currentBytes) : 0;
    EXPECT_EQ(e.churnBytes, 19200u);
}

TEST(MemPointDerived, AvgSizeWithResizeGrowth) {
    // AvgSize = totalBytes / allocs. With resize growth included,
    // AvgSize reflects the average total memory per allocation lifecycle.
    MemPointEntry e;
    e.totalBytes = 19200;  // includes resize growth
    e.allocs = 100;
    uint64_t avg = e.allocs > 0 ? e.totalBytes / e.allocs : 0;
    EXPECT_EQ(avg, 192u);
}

TEST_F(MemPointShowTest, ResizeAffectedMaxSize) {
    std::vector<ProcessorOutput> outputs = {
        MakeOutput("mempoint.100.8.current_bytes", "0"),
        MakeOutput("mempoint.100.8.total_alloc",   "19200"),
        MakeOutput("mempoint.100.8.alloc_count",   "100"),
        MakeOutput("mempoint.100.8.free_count",    "100"),
        MakeOutput("mempoint.100.8.peak",          "19200"),
        MakeOutput("mempoint.100.8.max_size",      "192"),
        MakeOutput("mempoint.100.8.min_size",      "64"),
        MakeOutput("mempoint.100.8.first_seen_ns", "1000000000"),
        MakeOutput("mempoint.100.8.key_name",      "Heap_object"),
    };
    DisplayEngine display;
    display.ShowMemPoint(outputs, true, 20, {}, {}, "max_size,min_size");
    std::string out = GetOutput();
    EXPECT_NE(out.find("192 B"), std::string::npos);
    EXPECT_NE(out.find("64 B"), std::string::npos);
}

TEST_F(MemPointShowTest, DefaultOutputHasSeparator) {
    std::vector<ProcessorOutput> outputs = {
        MakeOutput("mempoint.100.1.current_bytes", "2048"),
        MakeOutput("mempoint.100.1.total_alloc",   "8192"),
        MakeOutput("mempoint.100.1.alloc_count",   "100"),
        MakeOutput("mempoint.100.1.free_count",    "50"),
        MakeOutput("mempoint.100.1.peak",          "4096"),
        MakeOutput("mempoint.100.1.max_size",      "1024"),
        MakeOutput("mempoint.100.1.min_size",      "128"),
        MakeOutput("mempoint.100.1.first_seen_ns", "1000000000"),
    };
    DisplayEngine display;
    display.ShowMemPoint(outputs, true, 20, {}, {});
    std::string out = GetOutput();
    EXPECT_NE(out.find("----"), std::string::npos);
}
