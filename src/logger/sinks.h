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

#include "spdiag/log_sink.h"

#include <string>
#include <fstream>

namespace SpDiag {

class ConsoleSink : public LogSink {
public:
    void Write(const std::string& message) override;
    void Flush() override;
};

class FileSink : public LogSink {
public:
    FileSink(const std::string& filename, size_t max_size, int max_files);
    ~FileSink() override;

    void Write(const std::string& message) override;
    void Flush() override;

private:
    void Rotate();

    std::ofstream file_;
    std::string filename_;
    size_t max_size_;
    int max_files_;
    size_t current_size_;
};

} // namespace SpDiag
