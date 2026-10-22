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

#include <chrono>
#include <optional>
#include <string>

namespace SpDiag {

class FileLockGuard {
public:
    FileLockGuard(const FileLockGuard&) = delete;
    FileLockGuard& operator=(const FileLockGuard&) = delete;
    FileLockGuard(FileLockGuard&& other) noexcept;
    FileLockGuard& operator=(FileLockGuard&& other) noexcept;
    ~FileLockGuard();

    static std::optional<FileLockGuard> AcquireFor(
        const std::string& lockName, std::chrono::milliseconds timeout,
        int* errorCode = nullptr);

private:
    explicit FileLockGuard(int fd) : fd_(fd) {}
    int fd_ = -1;
};

std::string MakeLockName(const char* domain, const std::string& key);

}  // namespace SpDiag
