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

#include "src/cli/csv_writer.h"
#include "src/cli/text_table.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

using SpDiag::Cli::CsvWriter;
using SpDiag::Cli::TextTable;

namespace {

class TempDir {
public:
    TempDir() {
        path_ = std::filesystem::temp_directory_path() /
            ("spdiag_csv_writer_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path_);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    const std::filesystem::path& Path() const { return path_; }

private:
    std::filesystem::path path_;
};

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

}  // namespace

TEST(CsvWriterTest, EscapesCommaQuoteAndNewline) {
    TempDir temp;
    const auto csvPath = temp.Path() / "escape.csv";

    CsvWriter writer;
    ASSERT_TRUE(writer.Open(csvPath.string()));
    writer.WriteHeader({"plain", "comma", "quote", "newline"});
    writer.WriteRow({"value", "a,b", "a\"b", "a\nb"});
    writer.Close();

    EXPECT_EQ(ReadFile(csvPath),
              "plain,comma,quote,newline\n"
              "value,\"a,b\",\"a\"\"b\",\"a\nb\"\n");
}

TEST(CsvWriterTest, TextTableMirrorsHeaderAndRows) {
    TempDir temp;
    const auto csvPath = temp.Path() / "table.csv";

    CsvWriter writer;
    ASSERT_TRUE(writer.Open(csvPath.string()));
    CsvWriter::SetActive(&writer);

    std::ostringstream terminal;
    auto* oldCout = std::cout.rdbuf(terminal.rdbuf());
    {
        TextTable table;
        table.AddColumn("Name", 8);
        table.AddColumn("Cost(ns)", 10, true);
        table.PrintHeader();
        table.PrintRow({"demo", "42"});
    }
    std::cout.rdbuf(oldCout);
    CsvWriter::SetActive(nullptr);
    writer.Close();

    EXPECT_EQ(ReadFile(csvPath), "Name,Cost(ns)\ndemo,42\n");
    EXPECT_EQ(terminal.str().find(" | "), std::string::npos);
    EXPECT_NE(terminal.str().find("        42\n"), std::string::npos);
}

TEST(CsvWriterTest, PersistentSessionReusesOneFile) {
    TempDir temp;
    const auto outputDir = temp.Path() / "nested" / "results";

    CsvWriter writer;
    writer.SetOutputDir(outputDir.string());
    writer.SetPersistent(true);

    ASSERT_EQ(writer.BeginSession("watch"), &writer);
    const std::string firstPath = writer.FilePath();
    writer.WriteLine("first");
    writer.EndSession();

    ASSERT_EQ(writer.BeginSession("ignored_second_prefix"), &writer);
    EXPECT_EQ(writer.FilePath(), firstPath);
    writer.WriteLine("second");
    writer.ForceEndSession();

    size_t csvCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(outputDir)) {
        if (entry.path().extension() == ".csv") {
            ++csvCount;
        }
    }
    EXPECT_EQ(csvCount, 1U);
    EXPECT_EQ(ReadFile(firstPath), "first\nsecond\n");
}
