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

#include "file_lock_guard.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace SpDiag {
namespace {
constexpr uint64_t fnvOffset = 14695981039346656037ULL;
constexpr uint64_t fnvPrime = 1099511628211ULL;
uint64_t StableHash(const std::string& value) {
    uint64_t hash = fnvOffset;
    for (unsigned char ch : value) {
        hash ^= ch;
        hash *= fnvPrime;
    }
    return hash;
}
}  // namespace

FileLockGuard::FileLockGuard(FileLockGuard&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

FileLockGuard& FileLockGuard::operator=(FileLockGuard&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) {
            close(fd_);
        }
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

FileLockGuard::~FileLockGuard() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

std::optional<FileLockGuard> FileLockGuard::AcquireFor(
    const std::string& lockName, std::chrono::milliseconds timeout,
    int* errorCode) {
    if (errorCode != nullptr) {
        *errorCode = 0;
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    int fd = -1;
    while (fd < 0) {
        fd = shm_open(lockName.c_str(), O_RDWR | O_CLOEXEC, 0);
        int openError = errno;
        if (fd < 0 && openError == ENOENT) {
            fd = shm_open(lockName.c_str(),
                          O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
            openError = errno;
        }
        if (fd < 0 && openError == EEXIST)
            fd = shm_open(lockName.c_str(), O_RDWR | O_CLOEXEC, 0);
        if (fd >= 0) break;
        if ((openError != EACCES && openError != EEXIST) ||
            timeout.count() == 0 ||
            std::chrono::steady_clock::now() >= deadline) {
            if (errorCode != nullptr) *errorCode = openError;
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (fchmod(fd, 0666) != 0 && errno != EPERM) {
        const int chmodError = errno;
        close(fd);
        if (errorCode != nullptr) {
            *errorCode = chmodError;
        }
        return std::nullopt;
    }
    struct flock lock {};
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 1;

    while (fcntl(fd, F_OFD_SETLK, &lock) != 0) {
        const int lockError = errno;
        if (lockError != EAGAIN && lockError != EACCES) {
            close(fd);
            if (errorCode != nullptr) {
                *errorCode = lockError;
            }
            return std::nullopt;
        }
        if (timeout.count() == 0 ||
            std::chrono::steady_clock::now() >= deadline) {
            close(fd);
            if (errorCode != nullptr) {
                *errorCode = ETIMEDOUT;
            }
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return FileLockGuard(fd);
}

std::string MakeLockName(const char* domain, const std::string& key) {
    char name[96];
    std::snprintf(name, sizeof(name), "/spdiag_lock_%s_%016llx", domain,
                  static_cast<unsigned long long>(StableHash(key)));
    return name;
}

std::string MakeBoundedLockName(const char* domain, const std::string& key,
                                uint64_t bucketCount) {
    if (bucketCount == 0) return MakeLockName(domain, key);
    char name[96];
    std::snprintf(name, sizeof(name), "/spdiag_lock_%s_bucket_%04llx", domain,
                  static_cast<unsigned long long>(
                      StableHash(key) % bucketCount));
    return name;
}

}  // namespace SpDiag
