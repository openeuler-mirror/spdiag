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

#include <iostream>
#include <cstdio>
#include <cstring>
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
        mkdir(dir.c_str(), 0755);
    }

    file_.open(filename_, std::ios::out | std::ios::app);
    if (file_.is_open()) {
        file_.seekp(0, std::ios::end);
        current_size_ = static_cast<size_t>(file_.tellp());
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
    }

    file_.write(line.data(), static_cast<std::streamsize>(line.size()));
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
        std::rename(old_name.c_str(), new_name.c_str());
    }

    std::string backup_name = filename_ + ".1";
    std::rename(filename_.c_str(), backup_name.c_str());

    file_.open(filename_, std::ios::out | std::ios::trunc);
    current_size_ = 0;
}

} // namespace SpDiag
