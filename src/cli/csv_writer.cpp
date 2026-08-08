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

#include "csv_writer.h"
#include <vector>
#include <memory>

namespace SpDiag {
namespace Cli {

bool CsvWriter::EnsureDir(const std::string& dir) {
    if (dir.empty() || dir == ".") return true;

    // 递归逐层创建，支持多层路径
    std::string built;
    for (size_t i = 0; i < dir.size(); ++i) {
        char c = dir[i];
        if (c == '/' || c == '\\') {
            if (!built.empty() && built != "/") {
                mkdir(built.c_str(), 0755);
            }
        }
        built += c;
    }
    // 最后一级
    if (!built.empty() && built != "/") {
        mkdir(built.c_str(), 0755);
    }
    return true;  // best-effort，即使部分失败也继续
}

CsvWriter* CsvWriter::BeginSession(const std::string& prefix) {
    // 持久模式下已打开则跳过，避免每轮迭代创建新文件
    if (persistent_ && ofs_.is_open()) return this;

    // 关闭上一个会话
    if (ofs_.is_open()) {
        ofs_.flush();
        ofs_.close();
    }
    SetActive(nullptr);

    if (outputDir_.empty()) return nullptr;

    EnsureDir(outputDir_);
    std::string path = MakeFilename(outputDir_, prefix);

    if (!Open(path)) return nullptr;

    SetActive(this);
    return this;
}

void CsvWriter::EndSession() {
    // 持久模式下延迟关闭，由外部 ForceEndSession 统一关闭
    if (persistent_) return;

    SetActive(nullptr);
    if (ofs_.is_open()) {
        ofs_.flush();
        ofs_.close();
    }
}

}  // namespace Cli
}  // namespace SpDiag
