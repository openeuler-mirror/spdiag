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

#include "private_storage.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>

namespace SpDiag {
namespace {

void SetError(std::string* error, const std::string& message) {
    if (error != nullptr) *error = message;
}

bool CheckDirectory(const std::filesystem::path& path, bool target,
                    bool create, std::string* error) {
    struct stat info {};
    if (lstat(path.c_str(), &info) != 0) {
        if (errno != ENOENT || !create || mkdir(path.c_str(), 0700) != 0) {
            SetError(error, "cannot create private directory " + path.string() +
                ": " + std::strerror(errno));
            return false;
        }
        if (lstat(path.c_str(), &info) != 0) {
            SetError(error, "cannot inspect private directory " + path.string());
            return false;
        }
    }
    if (!S_ISDIR(info.st_mode) || S_ISLNK(info.st_mode)) {
        SetError(error, "private storage path is not a directory: " +
            path.string());
        return false;
    }
    const uid_t uid = geteuid();
    if (target) {
        if (info.st_uid != uid || chmod(path.c_str(), 0700) != 0) {
            SetError(error, "private storage directory has the wrong owner: " +
                path.string());
            return false;
        }
    } else if (info.st_uid != 0 && info.st_uid != uid) {
        SetError(error, "private storage ancestor has the wrong owner: " +
            path.string());
        return false;
    } else if ((info.st_mode & S_IWOTH) != 0 &&
               (info.st_mode & S_ISVTX) == 0) {
        SetError(error, "private storage ancestor is world-writable: " +
            path.string());
        return false;
    }
    return true;
}

bool CheckDirectoryChain(const std::filesystem::path& directory, bool create,
                         std::string* error) {
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(directory, ec).lexically_normal();
    if (ec || absolute.empty() || !absolute.has_root_path()) {
        SetError(error, "invalid private storage directory: " + directory.string());
        return false;
    }

    std::filesystem::path current = absolute.root_path();
    for (const auto& component : absolute.relative_path()) {
        if (component == "." || component == "..") {
            SetError(error, "private storage path contains traversal");
            return false;
        }
        current /= component;
        const bool target = current == absolute;
        if (!CheckDirectory(current, target, create, error)) return false;
    }
    return true;
}

bool CheckFile(const std::string& path, bool allowMissing,
               std::string* error) {
    struct stat info {};
    if (lstat(path.c_str(), &info) != 0) {
        if (allowMissing && errno == ENOENT) return true;
        SetError(error, "cannot inspect private file " + path + ": " +
            std::strerror(errno));
        return false;
    }
    if (!S_ISREG(info.st_mode) || info.st_uid != geteuid()) {
        SetError(error, "private file is not an owner-controlled regular file: " +
            path);
        return false;
    }
    return true;
}

}  // namespace

bool PreparePrivateFile(const std::string& path, std::string* error) {
    const std::filesystem::path filePath(path);
    return CheckDirectoryChain(filePath.parent_path(), true, error) &&
           CheckFile(path, true, error);
}

bool ValidatePrivateFile(const std::string& path, std::string* error) {
    const std::filesystem::path filePath(path);
    return CheckDirectoryChain(filePath.parent_path(), false, error) &&
           CheckFile(path, false, error);
}

bool RestrictPrivateFile(const std::string& path, std::string* error) {
    if (chmod(path.c_str(), 0600) == 0) return true;
    SetError(error, "cannot set private file permissions for " + path + ": " +
        std::strerror(errno));
    return false;
}

}  // namespace SpDiag
