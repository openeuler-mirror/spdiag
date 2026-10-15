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

namespace SpDiag {

class RegistryFileGuard {
public:
    static std::optional<RegistryFileGuard> AcquireFor(
        int fd, std::chrono::milliseconds timeout,
        int* errorCode = nullptr) noexcept;

    RegistryFileGuard(const RegistryFileGuard&) = delete;
    RegistryFileGuard& operator=(const RegistryFileGuard&) = delete;
    RegistryFileGuard(RegistryFileGuard&& other) noexcept;
    RegistryFileGuard& operator=(RegistryFileGuard&& other) noexcept;
    ~RegistryFileGuard();

private:
    explicit RegistryFileGuard(int fd) noexcept : fd_(fd) {}
    void Release() noexcept;

    int fd_ = -1;
};

}  // namespace SpDiag
