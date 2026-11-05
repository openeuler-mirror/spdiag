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

#include "spdiag/shm_path.h"

#include <cctype>
#include <cstdlib>

namespace SpDiag {

bool NormalizeShmPath(const std::string& input, std::string& shmName,
                      std::string& error) {
    std::string value = input.empty() ? defaultShmName : input;
    constexpr const char* prefix = "/dev/shm/";
    if (value.rfind(prefix, 0) == 0) {
        value.erase(0, std::char_traits<char>::length(prefix));
    } else if (!value.empty() && value.front() == '/') {
        value.erase(0, 1);
    }
    if (value.empty() || value == "." || value == ".." ||
        value.size() > 200 || value.find('/') != std::string::npos) {
        error = "expected NAME, /NAME, or /dev/shm/NAME";
        return false;
    }
    for (unsigned char c : value) {
        const bool portable = std::isalnum(c) || c == '_' || c == '-' || c == '.';
        if (c < 0x80 && !portable) {
            error = "SHM name contains whitespace, control characters or punctuation";
            return false;
        }
    }
    shmName = "/" + value;
    return true;
}

bool ResolveSdkShmName(const std::string& configured, std::string& shmName,
                       std::string& error) {
    std::string selected = configured.empty() ? defaultShmName : configured;
    const char* env = std::getenv(shmPathEnvironment);
    if ((configured.empty() || configured == defaultShmName) &&
        env != nullptr && env[0] != '\0') {
        selected = env;
    }
    return NormalizeShmPath(selected, shmName, error);
}

std::string ShmFilesystemPath(const std::string& shmName) {
    return "/dev/shm" + shmName;
}

std::string ShmScopedDataDir(const std::string& baseDir,
                             const std::string& shmName, uint32_t uid) {
    if (shmName == defaultShmName) return baseDir;
    std::string root = baseDir.empty() ? "/tmp/spdiag_data" : baseDir;
    while (root.size() > 1 && root.back() == '/') root.pop_back();
    return root + "-uid-" + std::to_string(uid) + shmName;
}

}
