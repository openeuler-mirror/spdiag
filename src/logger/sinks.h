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
#include <mutex>

namespace SpDiag {

class ConsoleSink : public LogSink {
public:
    void Write(const std::string& message) override;
    void Flush() override;

private:
    // ERROR 同步路径与后台 worker 线程可能并发写同一 sink，需互斥串行化。
    std::mutex mutex_;
};

class FileSink : public LogSink {
public:
    FileSink(const std::string& filename, size_t max_size, int max_files);
    ~FileSink() override;

    void Write(const std::string& message) override;
    void Flush() override;

private:
    // 仅由 Write（持锁）调用，不再单独加锁。
    void Rotate();

    std::mutex mutex_;
    std::ofstream file_;
    std::string filename_;
    size_t max_size_;
    int max_files_;
    size_t current_size_;
    // 写失败告警去重标记：磁盘持续故障时每个消息都会写失败，
    // 只在首次失败时告警一次，磁盘恢复（写成功）后重置，避免刷屏。
    bool write_error_reported_ = false;
};

} // namespace SpDiag
