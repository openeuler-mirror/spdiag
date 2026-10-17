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

#include "registry_file_guard.h"

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <utility>

#ifndef F_OFD_SETLK
#error "SpDiag requires Linux F_OFD_SETLK support"
#endif

namespace SpDiag {

namespace {

bool SetRegistryLock(int fd, short type) noexcept {
    struct flock lock {};
    lock.l_type = type;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 1;
    return fcntl(fd, F_OFD_SETLK, &lock) == 0;
}

}  // namespace

std::optional<RegistryFileGuard> RegistryFileGuard::AcquireFor(
    int fd, std::chrono::milliseconds timeout, int* errorCode) noexcept {
    if (errorCode != nullptr) {
        *errorCode = 0;
    }
    if (fd < 0) {
        if (errorCode != nullptr) {
            *errorCode = EBADF;
        }
        return std::nullopt;
    }

    const auto boundedTimeout = std::max(timeout, std::chrono::milliseconds::zero());
    const auto deadline = std::chrono::steady_clock::now() + boundedTimeout;
    while (!SetRegistryLock(fd, F_WRLCK)) {
        const int currentError = errno;
        if (currentError != EACCES && currentError != EAGAIN &&
            currentError != EINTR) {
            if (errorCode != nullptr) {
                *errorCode = currentError;
            }
            return std::nullopt;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            if (errorCode != nullptr) {
                *errorCode = ETIMEDOUT;
            }
            return std::nullopt;
        }
        (void)poll(nullptr, 0, 1);
    }
    return RegistryFileGuard(fd);
}

RegistryFileGuard::RegistryFileGuard(RegistryFileGuard&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)) {}

RegistryFileGuard& RegistryFileGuard::operator=(RegistryFileGuard&& other) noexcept {
    if (this != &other) {
        Release();
        fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
}

RegistryFileGuard::~RegistryFileGuard() {
    Release();
}

void RegistryFileGuard::Release() noexcept {
    if (fd_ >= 0) {
        (void)SetRegistryLock(fd_, F_UNLCK);
        fd_ = -1;
    }
}

}  // namespace SpDiag
