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
#include <vector>
#include <string>
#include <iostream>
#include <algorithm>
#include "csv_writer.h"

namespace SpDiag {
namespace Cli {

class TextTable {
public:
    TextTable() {
        csv_ = CsvWriter::Active();
    }

    void AddColumn(const std::string& header, int width, bool rightAlign = false) {
        columns_.push_back({header, width, rightAlign, width});
    }

    // Fixed-width immediate output
    void PrintHeader() const {
        std::string line;
        for (size_t i = 0; i < columns_.size(); ++i) {
            if (i > 0) line += "  ";
            else line += " ";
            line += PadCell(columns_[i].header, columns_[i].minWidth, false);
        }
        std::cout << line << "\n";

        std::string sep;
        for (size_t i = 0; i < columns_.size(); ++i) {
            if (i > 0) sep += "  ";
            else sep += " ";
            sep += std::string(columns_[i].minWidth, '-');
        }
        std::cout << sep << "\n";

        // CSV 镜像：表头
        if (csv_ && csv_->IsOpen() && !csvHeaderWritten_) {
            std::vector<std::string> headers;
            for (const auto& col : columns_) headers.push_back(col.header);
            csv_->WriteHeader(headers);
            csvHeaderWritten_ = true;
        }
    }

    void PrintRow(const std::vector<std::string>& cells) const {
        std::string line;
        for (size_t i = 0; i < columns_.size(); ++i) {
            if (i > 0) line += "  ";
            else line += " ";
            const std::string& val = (i < cells.size()) ? cells[i] : "";
            line += PadCell(val, columns_[i].minWidth, columns_[i].rightAlign);
        }
        std::cout << line << "\n";

        // CSV 镜像：行数据
        if (csv_ && csv_->IsOpen()) {
            csv_->WriteRow(cells);
        }
    }

    // Dynamic-width deferred output
    void AddRow(const std::vector<std::string>& cells) {
        rows_.push_back(cells);
        for (size_t i = 0; i < columns_.size() && i < cells.size(); ++i) {
            int cellLen = static_cast<int>(cells[i].size());
            if (cellLen > columns_[i].actualWidth) {
                columns_[i].actualWidth = cellLen;
            }
        }
    }

    void Print() {
        std::string headerLine;
        for (size_t i = 0; i < columns_.size(); ++i) {
            if (i > 0) headerLine += "  ";
            else headerLine += " ";
            headerLine += PadCell(columns_[i].header, columns_[i].actualWidth, false);
        }
        std::cout << headerLine << "\n";

        std::string sepLine;
        for (size_t i = 0; i < columns_.size(); ++i) {
            if (i > 0) sepLine += "  ";
            else sepLine += " ";
            sepLine += std::string(columns_[i].actualWidth, '-');
        }
        std::cout << sepLine << "\n";

        for (const auto& row : rows_) {
            std::string rowLine;
            for (size_t i = 0; i < columns_.size(); ++i) {
                if (i > 0) rowLine += "  ";
                else rowLine += " ";
                const std::string& val = (i < row.size()) ? row[i] : "";
                rowLine += PadCell(val, columns_[i].actualWidth, columns_[i].rightAlign);
            }
            std::cout << rowLine << "\n";
        }

        // CSV 镜像：延迟模式一次性输出表头+所有行
        if (csv_ && csv_->IsOpen()) {
            std::vector<std::string> headers;
            for (const auto& col : columns_) headers.push_back(col.header);
            csv_->WriteHeader(headers);
            for (const auto& row : rows_) {
                csv_->WriteRow(row);
            }
        }
    }

private:
    struct Column {
        std::string header;
        int minWidth;
        bool rightAlign;
        int actualWidth;
    };
    std::vector<Column> columns_;
    std::vector<std::vector<std::string>> rows_;
    CsvWriter* csv_ = nullptr;
    mutable bool csvHeaderWritten_ = false;

    static std::string PadCell(const std::string& s, int width, bool rightAlign) {
        int len = static_cast<int>(s.size());
        int pad = width - len;
        if (pad <= 0) return s;
        if (rightAlign) return std::string(pad, ' ') + s;
        return s + std::string(pad, ' ');
    }
};

}  // namespace Cli
}  // namespace SpDiag
