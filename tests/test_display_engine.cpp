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
#include "src/runtime/data_pipeline.h"
#include <sstream>
#include <string>
#include <map>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace SpDiag;
using namespace SpDiag::Cli;

namespace {

std::vector<ProcessorOutput> MakeOutput(const std::map<std::string, std::string>& kv) {
    std::vector<ProcessorOutput> outputs;
    for (const auto& [k, v] : kv) {
        outputs.push_back({0, "", "", k, v});
    }
    return outputs;
}

std::string CaptureShowObMemory(
    DisplayEngine& engine,
    const std::vector<ProcessorOutput>& outputs,
    bool humanReadable = true,
    bool noFilter = false,
    bool detail = false,
    const std::string& groupBy = "tid+frame",
    int top = 20,
    const std::string& showFields = "",
    const std::vector<SortKey>& sortKeys = {},
    const std::vector<std::string>& filterGlobs = {})
{
    std::ostringstream oss;
    auto* oldCout = std::cout.rdbuf(oss.rdbuf());
    auto* oldCerr = std::cerr.rdbuf(oss.rdbuf());
    engine.ShowObMemory(outputs, humanReadable, noFilter, detail,
                        groupBy, top, showFields, sortKeys, filterGlobs);
    std::cerr.rdbuf(oldCerr);
    std::cout.rdbuf(oldCout);
    return oss.str();
}

std::string CaptureShowObMemory(
    const std::vector<ProcessorOutput>& outputs,
    bool humanReadable = true,
    bool noFilter = false,
    bool detail = false,
    const std::string& groupBy = "tid+frame",
    int top = 20,
    const std::string& showFields = "",
    const std::vector<SortKey>& sortKeys = {},
    const std::vector<std::string>& filterGlobs = {})
{
    DisplayEngine engine;
    return CaptureShowObMemory(engine, outputs, humanReadable, noFilter, detail,
                               groupBy, top, showFields, sortKeys, filterGlobs);
}

} // namespace

TEST(DisplayEngineTest, ParseMemEntries_BasicFields) {
    auto outputs = MakeOutput({
        {"mem.pid.123.tid.456.stack.1.current_bytes", "1024"},
        {"mem.pid.123.tid.456.stack.1.total_alloc_bytes", "5000"},
        {"mem.pid.123.tid.456.stack.1.alloc_count", "10"},
        {"mem.pid.123.tid.456.stack.1.free_count", "5"},
        {"mem.pid.123.tid.456.stack.1.peak_bytes", "2048"},
        {"mem.pid.123.tid.456.stack.1.max_size", "1024"},
        {"mem.pid.123.tid.456.stack.1.min_size", "64"},
        {"mem.pid.123.tid.456.stack.1.first_seen_ns", "1000000000"},
        {"mem.pid.123.tid.456.stack.1.stack_trace", "func1 <- func2"},
    });

    auto out = CaptureShowObMemory(outputs);

    EXPECT_TRUE(out.find("123") != std::string::npos);
    EXPECT_TRUE(out.find("456") != std::string::npos);
    EXPECT_TRUE(out.find("func1") != std::string::npos);
    EXPECT_TRUE(out.find("10") != std::string::npos);
    EXPECT_TRUE(out.find("5") != std::string::npos);
    EXPECT_TRUE(out.find("1") != std::string::npos);
}

TEST(DisplayEngineTest, ParseMemEntries_MultiEntry) {
    auto outputs = MakeOutput({
        {"mem.pid.100.tid.200.stack.1.current_bytes", "100"},
        {"mem.pid.100.tid.200.stack.1.total_alloc_bytes", "200"},
        {"mem.pid.100.tid.200.stack.1.alloc_count", "3"},
        {"mem.pid.100.tid.200.stack.1.stack_trace", "FuncAlpha <- libAlpha"},
        {"mem.pid.300.tid.400.stack.2.current_bytes", "500"},
        {"mem.pid.300.tid.400.stack.2.total_alloc_bytes", "600"},
        {"mem.pid.300.tid.400.stack.2.alloc_count", "8"},
        {"mem.pid.300.tid.400.stack.2.stack_trace", "FuncBeta <- libBeta"},
    });

    auto out = CaptureShowObMemory(outputs);

    EXPECT_TRUE(out.find("FuncAlpha") != std::string::npos);
    EXPECT_TRUE(out.find("FuncBeta") != std::string::npos);
}

TEST(DisplayEngineTest, ParseMemEntries_SkipsNonMemPrefix) {
    auto outputs = MakeOutput({
        {"mem.pid.100.tid.200.stack.1.current_bytes", "100"},
        {"mem.pid.100.tid.200.stack.1.total_alloc_bytes", "200"},
        {"mem.pid.100.tid.200.stack.1.alloc_count", "3"},
        {"mem.pid.100.tid.200.stack.1.stack_trace", "MyFunc"},
        {"other.something", "ignore_me"},
        {"cache.L1-dcache.references", "9999"},
    });

    auto out = CaptureShowObMemory(outputs);

    EXPECT_TRUE(out.find("MyFunc") != std::string::npos);
}

TEST(DisplayEngineTest, Aggregate_FrameMode) {
    auto outputs = MakeOutput({
        {"mem.pid.100.tid.200.stack.1.current_bytes", "100"},
        {"mem.pid.100.tid.200.stack.1.total_alloc_bytes", "200"},
        {"mem.pid.100.tid.200.stack.1.alloc_count", "3"},
        {"mem.pid.100.tid.200.stack.1.stack_trace", "SharedFunc <- helper"},
        {"mem.pid.100.tid.300.stack.1.current_bytes", "150"},
        {"mem.pid.100.tid.300.stack.1.total_alloc_bytes", "250"},
        {"mem.pid.100.tid.300.stack.1.alloc_count", "4"},
        {"mem.pid.100.tid.300.stack.1.stack_trace", "SharedFunc <- helper"},
    });

    auto out = CaptureShowObMemory(outputs, true, false, false, "frame");

    EXPECT_TRUE(out.find("SharedFunc") != std::string::npos);
    EXPECT_TRUE(out.find("2 threads") != std::string::npos);
    EXPECT_TRUE(out.find("250") != std::string::npos || out.find("250 B") != std::string::npos);
}

TEST(DisplayEngineTest, ChurnCalculation) {
    auto outputs = MakeOutput({
        {"mem.pid.1.tid.1.stack.1.current_bytes", "2000"},
        {"mem.pid.1.tid.1.stack.1.total_alloc_bytes", "5000"},
        {"mem.pid.1.tid.1.stack.1.alloc_count", "10"},
        {"mem.pid.1.tid.1.stack.1.stack_trace", "CalcFunc"},
    });

    auto out = CaptureShowObMemory(outputs, false, false, false, "tid+frame",
                                   20, "churn");

    EXPECT_TRUE(out.find("3,000 B") != std::string::npos);
}

TEST(DisplayEngineTest, ChurnProtection) {
    auto outputs = MakeOutput({
        {"mem.pid.1.tid.1.stack.1.current_bytes", "2000"},
        {"mem.pid.1.tid.1.stack.1.total_alloc_bytes", "1000"},
        {"mem.pid.1.tid.1.stack.1.alloc_count", "5"},
        {"mem.pid.1.tid.1.stack.1.stack_trace", "BadFunc"},
    });

    auto out = CaptureShowObMemory(outputs, false, false, false, "tid+frame",
                                   20, "churn");

    EXPECT_TRUE(out.find("0 B") != std::string::npos);
}

TEST(DisplayEngineTest, FormatRate_DtSecZero) {
    auto outputs = MakeOutput({
        {"mem.pid.1.tid.1.stack.1.current_bytes", "1000"},
        {"mem.pid.1.tid.1.stack.1.total_alloc_bytes", "1000"},
        {"mem.pid.1.tid.1.stack.1.alloc_count", "1"},
        {"mem.pid.1.tid.1.stack.1.stack_trace", "RateFunc"},
    });

    auto out = CaptureShowObMemory(outputs, true, false, false, "tid+frame",
                                   20, "alloc_rate");

    EXPECT_TRUE(out.find("?") != std::string::npos);
}

TEST(DisplayEngineTest, ShowColumns_Custom) {
    auto outputs = MakeOutput({
        {"mem.pid.1.tid.1.stack.1.current_bytes", "1000"},
        {"mem.pid.1.tid.1.stack.1.total_alloc_bytes", "5000"},
        {"mem.pid.1.tid.1.stack.1.alloc_count", "10"},
        {"mem.pid.1.tid.1.stack.1.stack_trace", "CustomFunc"},
    });

    auto out = CaptureShowObMemory(outputs, true, false, false, "tid+frame",
                                   20, "current,total,allocs");

    EXPECT_TRUE(out.find("Current") != std::string::npos);
    EXPECT_TRUE(out.find("Total") != std::string::npos);
    EXPECT_TRUE(out.find("Allocs") != std::string::npos);
}

TEST(DisplayEngineTest, ShowColumns_AllThirteen) {
    auto outputs = MakeOutput({
        {"mem.pid.1.tid.1.stack.1.current_bytes", "1000"},
        {"mem.pid.1.tid.1.stack.1.total_alloc_bytes", "5000"},
        {"mem.pid.1.tid.1.stack.1.alloc_count", "10"},
        {"mem.pid.1.tid.1.stack.1.free_count", "3"},
        {"mem.pid.1.tid.1.stack.1.peak_bytes", "2000"},
        {"mem.pid.1.tid.1.stack.1.max_size", "1024"},
        {"mem.pid.1.tid.1.stack.1.min_size", "16"},
        {"mem.pid.1.tid.1.stack.1.first_seen_ns", "1000"},
        {"mem.pid.1.tid.1.stack.1.stack_trace", "AllColsFunc"},
    });

    auto out = CaptureShowObMemory(outputs, true, false, false, "tid+frame",
                                   20,
        "current,total,allocs,frees,peak,min_size,max_size,first_seen,leaked,avg_size,churn,churn_rate,alloc_rate");

    EXPECT_TRUE(out.find("Current") != std::string::npos);
    EXPECT_TRUE(out.find("Leaked") != std::string::npos);
    EXPECT_TRUE(out.find("AvgSize") != std::string::npos);
    EXPECT_TRUE(out.find("ChurnRate") != std::string::npos);
    EXPECT_TRUE(out.find("AllocRate") != std::string::npos);
}

TEST(DisplayEngineTest, Sort_Lambda) {
    auto outputs = MakeOutput({
        {"mem.pid.1.tid.1.stack.1.current_bytes", "100"},
        {"mem.pid.1.tid.1.stack.1.total_alloc_bytes", "200"},
        {"mem.pid.1.tid.1.stack.1.alloc_count", "1"},
        {"mem.pid.1.tid.1.stack.1.stack_trace", "FuncA"},
        {"mem.pid.1.tid.1.stack.2.current_bytes", "500"},
        {"mem.pid.1.tid.1.stack.2.total_alloc_bytes", "600"},
        {"mem.pid.1.tid.1.stack.2.alloc_count", "5"},
        {"mem.pid.1.tid.1.stack.2.stack_trace", "FuncB"},
    });

    std::vector<SortKey> sortKeys = {
        SortKey{SortKey::CURRENT, true}
    };

    auto out = CaptureShowObMemory(outputs, true, false, false, "tid+frame",
                                   20, "", sortKeys);

    auto posB = out.find("FuncB");
    auto posA = out.find("FuncA");
    ASSERT_TRUE(posB != std::string::npos);
    ASSERT_TRUE(posA != std::string::npos);
    EXPECT_LT(posB, posA);
}

TEST(DisplayEngineTest, GlobFilter_Pass) {
    auto outputs = MakeOutput({
        {"mem.pid.1.tid.1.stack.1.current_bytes", "1000"},
        {"mem.pid.1.tid.1.stack.1.total_alloc_bytes", "2000"},
        {"mem.pid.1.tid.1.stack.1.alloc_count", "5"},
        {"mem.pid.1.tid.1.stack.1.stack_trace", "MyWorkloadFunc <- helper"},
    });

    std::vector<std::string> filterGlobs = {"*Workload*"};

    auto out = CaptureShowObMemory(outputs, true, false, false, "tid+frame",
                                   20, "", {}, filterGlobs);

    EXPECT_TRUE(out.find("MyWorkloadFunc") != std::string::npos);
}

TEST(DisplayEngineTest, GlobFilter_Reject) {
    auto outputs = MakeOutput({
        {"mem.pid.1.tid.1.stack.1.current_bytes", "1000"},
        {"mem.pid.1.tid.1.stack.1.total_alloc_bytes", "2000"},
        {"mem.pid.1.tid.1.stack.1.alloc_count", "5"},
        {"mem.pid.1.tid.1.stack.1.stack_trace", "MyFunc <- helper"},
    });

    std::vector<std::string> filterGlobs = {"*Workload*"};

    auto out = CaptureShowObMemory(outputs, true, false, false, "tid+frame",
                                   20, "", {}, filterGlobs);

    EXPECT_TRUE(out.find("MyFunc") == std::string::npos);
    EXPECT_TRUE(out.find("(from 1 raw records)") != std::string::npos
                || out.find("groups=0") != std::string::npos);
}

TEST(DisplayEngineTest, TopTruncation) {
    std::map<std::string, std::string> kv;
    for (int i = 1; i <= 5; ++i) {
        std::string prefix = "mem.pid.1.tid.1.stack." + std::to_string(i);
        kv[prefix + ".current_bytes"] = std::to_string(1000 + i * 100);
        kv[prefix + ".total_alloc_bytes"] = std::to_string(2000 + i * 100);
        kv[prefix + ".alloc_count"] = std::to_string(i);
        kv[prefix + ".stack_trace"] = "Func" + std::to_string(i);
    }

    auto outputs = MakeOutput(kv);
    auto out = CaptureShowObMemory(outputs, true, false, false, "tid+frame",
                                   3);

    EXPECT_TRUE(out.find("[others]") != std::string::npos);
    EXPECT_TRUE(out.find("(2 groups)") != std::string::npos);
}

TEST(DisplayEngineTest, GroupBy_Raw) {
    auto outputs = MakeOutput({
        {"mem.pid.1.tid.100.stack.1.current_bytes", "300"},
        {"mem.pid.1.tid.100.stack.1.total_alloc_bytes", "400"},
        {"mem.pid.1.tid.100.stack.1.alloc_count", "2"},
        {"mem.pid.1.tid.100.stack.1.stack_trace", "RawFunc"},
    });

    auto out = CaptureShowObMemory(outputs, true, false, false, "raw");

    EXPECT_TRUE(out.find("RawFunc") != std::string::npos);
}

TEST(DisplayEngineTest, GroupBy_Frame) {
    auto outputs = MakeOutput({
        {"mem.pid.1.tid.100.stack.1.current_bytes", "300"},
        {"mem.pid.1.tid.100.stack.1.total_alloc_bytes", "400"},
        {"mem.pid.1.tid.100.stack.1.alloc_count", "2"},
        {"mem.pid.1.tid.100.stack.1.stack_trace", "FrameFunc"},
    });

    auto out = CaptureShowObMemory(outputs, true, false, false, "frame");

    EXPECT_TRUE(out.find("FrameFunc") != std::string::npos);
}

TEST(DisplayEngineTest, GroupBy_TidFrame) {
    auto outputs = MakeOutput({
        {"mem.pid.1.tid.100.stack.1.current_bytes", "300"},
        {"mem.pid.1.tid.100.stack.1.total_alloc_bytes", "400"},
        {"mem.pid.1.tid.100.stack.1.alloc_count", "2"},
        {"mem.pid.1.tid.100.stack.1.stack_trace", "TidFrameFunc"},
    });

    auto out = CaptureShowObMemory(outputs, true, false, false, "tid+frame");

    EXPECT_TRUE(out.find("TidFrameFunc") != std::string::npos);
}

TEST(DisplayEngineTest, FormatSize_Boundaries) {
    auto outputs = MakeOutput({
        {"mem.pid.1.tid.1.stack.1.current_bytes", "0"},
        {"mem.pid.1.tid.1.stack.1.total_alloc_bytes", "0"},
        {"mem.pid.1.tid.1.stack.1.alloc_count", "1"},
        {"mem.pid.1.tid.1.stack.1.stack_trace", "ZeroFunc"},
    });

    auto outHuman = CaptureShowObMemory(outputs, true, false, false, "tid+frame",
                                        20, "current");

    EXPECT_TRUE(outHuman.find("0 B") != std::string::npos);

    outputs = MakeOutput({
        {"mem.pid.1.tid.1.stack.1.current_bytes", "1024"},
        {"mem.pid.1.tid.1.stack.1.total_alloc_bytes", "1024"},
        {"mem.pid.1.tid.1.stack.1.alloc_count", "1"},
        {"mem.pid.1.tid.1.stack.1.stack_trace", "KiBFunc"},
    });

    outHuman = CaptureShowObMemory(outputs, true, false, false, "tid+frame",
                                   20, "current");

    EXPECT_TRUE(outHuman.find("1.00 KiB") != std::string::npos);

    outputs = MakeOutput({
        {"mem.pid.1.tid.1.stack.1.current_bytes", "1048576"},
        {"mem.pid.1.tid.1.stack.1.total_alloc_bytes", "1048576"},
        {"mem.pid.1.tid.1.stack.1.alloc_count", "1"},
        {"mem.pid.1.tid.1.stack.1.stack_trace", "MiBFunc"},
    });

    outHuman = CaptureShowObMemory(outputs, true, false, false, "tid+frame",
                                   20, "current");

    EXPECT_TRUE(outHuman.find("1.00 MiB") != std::string::npos);

    outputs = MakeOutput({
        {"mem.pid.1.tid.1.stack.1.current_bytes", "1073741824"},
        {"mem.pid.1.tid.1.stack.1.total_alloc_bytes", "1073741824"},
        {"mem.pid.1.tid.1.stack.1.alloc_count", "1"},
        {"mem.pid.1.tid.1.stack.1.stack_trace", "GiBFunc"},
    });

    outHuman = CaptureShowObMemory(outputs, true, false, false, "tid+frame",
                                   20, "current");

    EXPECT_TRUE(outHuman.find("1.00 GiB") != std::string::npos);

    outputs = MakeOutput({
        {"mem.pid.1.tid.1.stack.1.current_bytes", "1024"},
        {"mem.pid.1.tid.1.stack.1.total_alloc_bytes", "1024"},
        {"mem.pid.1.tid.1.stack.1.alloc_count", "1"},
        {"mem.pid.1.tid.1.stack.1.stack_trace", "NonHumanFunc"},
    });

    auto outNonHuman = CaptureShowObMemory(outputs, false, false, false, "tid+frame",
                                           20, "current");

    EXPECT_TRUE(outNonHuman.find("1,024 B") != std::string::npos);
}

TEST(DisplayEngineTest, LeakedColumn) {
    auto outputs = MakeOutput({
        {"mem.pid.1.tid.1.stack.1.current_bytes", "1000"},
        {"mem.pid.1.tid.1.stack.1.total_alloc_bytes", "5000"},
        {"mem.pid.1.tid.1.stack.1.alloc_count", "200"},
        {"mem.pid.1.tid.1.stack.1.free_count", "150"},
        {"mem.pid.1.tid.1.stack.1.stack_trace", "LeakFunc"},
    });

    auto out = CaptureShowObMemory(outputs, true, false, false, "tid+frame",
                                   20, "leaked");

    EXPECT_TRUE(out.find("50") != std::string::npos);

    outputs = MakeOutput({
        {"mem.pid.1.tid.1.stack.1.current_bytes", "1000"},
        {"mem.pid.1.tid.1.stack.1.total_alloc_bytes", "5000"},
        {"mem.pid.1.tid.1.stack.1.alloc_count", "100"},
        {"mem.pid.1.tid.1.stack.1.free_count", "150"},
        {"mem.pid.1.tid.1.stack.1.stack_trace", "NoLeakFunc"},
    });

    out = CaptureShowObMemory(outputs, true, false, false, "tid+frame",
                              20, "leaked");

    EXPECT_TRUE(out.find("0") != std::string::npos);
}

TEST(DisplayEngineTest, MinSize_UINT64MAX) {
    auto outputs = MakeOutput({
        {"mem.pid.1.tid.100.stack.1.current_bytes", "500"},
        {"mem.pid.1.tid.100.stack.1.total_alloc_bytes", "500"},
        {"mem.pid.1.tid.100.stack.1.alloc_count", "1"},
        {"mem.pid.1.tid.100.stack.1.stack_trace", "NoMinSizeFunc"},
        {"mem.pid.1.tid.200.stack.1.current_bytes", "300"},
        {"mem.pid.1.tid.200.stack.1.total_alloc_bytes", "300"},
        {"mem.pid.1.tid.200.stack.1.alloc_count", "1"},
        {"mem.pid.1.tid.200.stack.1.stack_trace", "NoMinSizeFunc"},
    });

    auto out = CaptureShowObMemory(outputs, false, false, false, "frame",
                                   20, "min_size");

    EXPECT_TRUE(out.find("0 B") != std::string::npos);
}

TEST(DisplayEngineTest, MemSessionStartNs) {
    DisplayEngine engine;

    auto outputs = MakeOutput({
        {"mem.pid.1.tid.1.stack.1.current_bytes", "1000"},
        {"mem.pid.1.tid.1.stack.1.total_alloc_bytes", "1000"},
        {"mem.pid.1.tid.1.stack.1.alloc_count", "1"},
        {"mem.pid.1.tid.1.stack.1.stack_trace", "SessionFunc"},
    });

    auto out1 = CaptureShowObMemory(engine, outputs, true, false, false,
                                    "tid+frame", 20, "alloc_rate");

    EXPECT_TRUE(out1.find("?") != std::string::npos);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto out2 = CaptureShowObMemory(engine, outputs, true, false, false,
                                    "tid+frame", 20, "alloc_rate");

    EXPECT_TRUE(out2.find("/s") != std::string::npos);
    EXPECT_TRUE(out2.find("?") == std::string::npos);
}

#ifdef SPDIAG_ENABLE_PERFLOG
TEST(DisplayEngineTest, ShowPerfLogUsesTextTableAndKeepsCsvContent) {
    DisplayEngine engine;
    auto tempDir = std::filesystem::temp_directory_path() /
        ("spdiag_perflog_table_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tempDir);
    engine.GetCsvWriter().SetOutputDir(tempDir.string());

    ShmInfo info{};
    info.numCores = 1;
    info.maxSlots = 1;
    info.perflogBootCounter = 0;
    info.perflogBootSystemNs = 0;
    info.perflogTimerFreq = 0;

    ShardBuffer shards;
    shards.resize(1, 1);
    PerfStat& stat = shards.stats(0)[0];
    stat.level = 3;
    stat.timestamps[0] = 1000000000ULL;
    stat.perflogCosts[0] = 42;
    stat.perflogThreadIds[0] = 7;
    stat.perflogWritePos.store(1, std::memory_order_relaxed);

    ProgramEntry program{};
    std::snprintf(program.name, sizeof(program.name), "%s", "demo");
    program.baseSlot = 0;
    program.slotCount = 1;
    program.pid = 123;
    program.active = 1;

    char pointNames[1][nameLen]{};
    char moduleNames[1][nameLen]{};
    std::snprintf(pointNames[0], nameLen, "%s", "point");
    std::snprintf(moduleNames[0], nameLen, "%s", "module");

    std::ostringstream oss;
    auto* oldCout = std::cout.rdbuf(oss.rdbuf());
    engine.ShowPerfLog(info, shards, &program, 1, pointNames, moduleNames);
    std::cout.rdbuf(oldCout);

    const std::string output = oss.str();
    EXPECT_EQ(output.find(" | "), std::string::npos);
    EXPECT_NE(output.find("Datetime"), std::string::npos);
    EXPECT_NE(output.find("module.point"), std::string::npos);
    EXPECT_NE(output.find("        42\n"), std::string::npos);

    std::ifstream csv(engine.GetCsvWriter().FilePath());
    std::vector<std::string> csvLines;
    for (std::string line; std::getline(csv, line);) {
        csvLines.push_back(line);
    }
    ASSERT_EQ(csvLines.size(), 2U);
    EXPECT_EQ(csvLines[0], "Datetime,PID,TID,CPU,Level,Program,Module.Point,Cost(ns)");
    EXPECT_NE(csvLines[1].find(",123,7,0,3,demo,module.point,42"), std::string::npos);

    std::filesystem::remove_all(tempDir);
}
#endif
