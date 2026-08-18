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

#include "sinks.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>

namespace SpDiag {

void ConsoleSink::Write(const std::string& message) {
    std::cerr << message << '\n';
}

void ConsoleSink::Flush() {
    std::cerr.flush();
}

FileSink::FileSink(const std::string& filename, size_t max_size, int max_files)
    : filename_(filename), max_size_(max_size), max_files_(max_files), current_size_(0) {
    size_t pos = filename_.find_last_of("/\\");
    if (pos != std::string::npos) {
        std::string dir = filename_.substr(0, pos);
        if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
            std::cerr << "[SpDiag] Failed to create log directory '" << dir
                      << "': " << std::strerror(errno) << "\n";
        }
    }

    file_.open(filename_, std::ios::out | std::ios::app);
    if (file_.is_open()) {
        file_.seekp(0, std::ios::end);
        current_size_ = static_cast<size_t>(file_.tellp());
    } else {
        // 打开失败不能静默：后续 Write 将全部丢弃，必须显式告警。
        std::cerr << "[SpDiag] Failed to open log file '" << filename_ << "'\n";
    }
}

FileSink::~FileSink() {
    if (file_.is_open()) {
        file_.close();
    }
}

void FileSink::Write(const std::string& message) {
    if (!file_.is_open()) {
        return;
    }

    std::string line = message + '\n';

    if (current_size_ + line.size() > max_size_) {
        Rotate();
        if (!file_.is_open()) {
            return;  // 轮转后仍未打开，本条消息无法写入
        }
    }

    file_.write(line.data(), static_cast<std::streamsize>(line.size()));
    if (file_.fail()) {
        // 写失败仅告警一次，避免磁盘持续故障时每个消息都向 stderr 输出；
        // 磁盘恢复（写成功）后重置标记。
        if (!write_error_reported_) {
            std::cerr << "[SpDiag] Failed to write log file '" << filename_ << "'\n";
            write_error_reported_ = true;
        }
        file_.clear();  // 清除错误标志，允许后续重试
        return;
    }
    write_error_reported_ = false;
    current_size_ += line.size();
}

void FileSink::Flush() {
    if (file_.is_open()) {
        file_.flush();
    }
}

void FileSink::Rotate() {
    file_.close();

    for (int i = max_files_ - 1; i >= 1; --i) {
        std::string old_name = filename_ + "." + std::to_string(i);
        std::string new_name = filename_ + "." + std::to_string(i + 1);
        if (std::rename(old_name.c_str(), new_name.c_str()) != 0 && errno != ENOENT) {
            std::cerr << "[SpDiag] Failed to rotate '" << old_name << "' -> '"
                      << new_name << "': " << std::strerror(errno) << "\n";
        }
    }

    // 先移走当前文件，再重新创建主文件。
    std::string backup_name = filename_ + ".1";
    // 移走当前文件；源文件不存在（首次轮转或从未成功创建）视为正常。
    if (std::rename(filename_.c_str(), backup_name.c_str()) != 0 && errno != ENOENT) {
        std::cerr << "[SpDiag] Failed to rotate '" << filename_ << "' -> '"
                  << backup_name << "': " << std::strerror(errno) << "\n";
    }

    // 统一使用追加模式打开：
    //   - 轮转成功：主文件名已不存在，append 会创建新空文件（等价于 trunc）；
    //   - 轮转失败：append 保留原文件内容，避免 trunc 破坏未移走的旧数据。
    file_.open(filename_, std::ios::out | std::ios::app);
    if (file_.is_open()) {
        file_.seekp(0, std::ios::end);
        current_size_ = static_cast<size_t>(file_.tellp());
    } else {
        std::cerr << "[SpDiag] Failed to reopen log file '" << filename_
                  << "' after rotation\n";
    }
}

} // namespace SpDiag
