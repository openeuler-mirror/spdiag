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
#include <vector>
#include <fstream>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <sys/stat.h>

namespace SpDiag {
namespace Cli {

class CsvWriter {
public:
    CsvWriter() = default;
    ~CsvWriter() { Close(); }

    // ---- 输出目录 & 会话管理 ----
    void SetOutputDir(const std::string& dir) { outputDir_ = dir; }
    const std::string& OutputDir() const { return outputDir_; }

    // watch 模式持久会话：同一会话内多次 BeginSession/EndSession 只创建/关闭一次文件
    void SetPersistent(bool p) { persistent_ = p; }

    CsvWriter* BeginSession(const std::string& prefix);
    void EndSession();
    void ForceEndSession() { Close(); SetActive(nullptr); }

    // ---- 单文件操作 ----
    bool Open(const std::string& filePath) {
        ofs_.open(filePath, std::ios::out | std::ios::trunc);
        if (!ofs_.is_open()) {
            std::cerr << "Warning: failed to open CSV output file: " << filePath << "\n";
            return false;
        }
        filePath_ = filePath;
        return true;
    }

    const std::string& FilePath() const { return filePath_; }
    bool IsOpen() const { return ofs_.is_open(); }

    // ---- 全局桥接：TextTable 构造时自动获取 ----
    static CsvWriter* Active() { return s_active_; }
    static void SetActive(CsvWriter* w) { s_active_ = w; }

    // 单行写入（给非表格式输出用，自动转义）
    void WriteLine(const std::string& line) {
        if (!ofs_.is_open()) return;
        ofs_ << EscapeCell(line) << '\n';
    }

    void WriteHeader(const std::vector<std::string>& headers) {
        WriteRowInternal(headers);
    }

    void WriteRow(const std::vector<std::string>& cells) {
        WriteRowInternal(cells);
    }

    void Close() {
        if (ofs_.is_open()) {
            ofs_.flush();
            ofs_.close();
        }
    }

    static std::string MakeFilename(const std::string& dir, const std::string& prefix) {
        auto t = std::time(nullptr);
        auto tm = *std::localtime(&t);
        std::ostringstream oss;
        oss << prefix << "_"
            << std::setfill('0')
            << std::setw(4) << (tm.tm_year + 1900)
            << std::setw(2) << (tm.tm_mon + 1)
            << std::setw(2) << tm.tm_mday << "_"
            << std::setw(2) << tm.tm_hour
            << std::setw(2) << tm.tm_min
            << std::setw(2) << tm.tm_sec
            << ".csv";
        std::string fname = oss.str();
        if (dir.empty() || dir == ".") return fname;
        std::string dirNorm = dir;
        if (dirNorm.back() != '/' && dirNorm.back() != '\\') {
            dirNorm += '/';
        }
        return dirNorm + fname;
    }

    static bool EnsureDir(const std::string& dir);

private:
    void WriteRowInternal(const std::vector<std::string>& cells) {
        for (size_t i = 0; i < cells.size(); ++i) {
            if (i > 0) ofs_ << ',';
            ofs_ << EscapeCell(cells[i]);
        }
        ofs_ << '\n';
    }

    static std::string EscapeCell(const std::string& cell) {
        bool needsQuoting = (cell.find(',') != std::string::npos ||
                             cell.find('"') != std::string::npos ||
                             cell.find('\n') != std::string::npos ||
                             cell.find('\r') != std::string::npos);
        if (!needsQuoting) return cell;

        std::string out;
        out.reserve(cell.size() + 2);
        out += '"';
        for (char c : cell) {
            if (c == '"') out += '"';
            out += c;
        }
        out += '"';
        return out;
    }

    std::ofstream ofs_;
    std::string filePath_;
    std::string outputDir_;
    bool persistent_ = false;
    inline static CsvWriter* s_active_ = nullptr;
};

}  // namespace Cli
}  // namespace SpDiag
