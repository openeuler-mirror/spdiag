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

#include <cstdint>
#include <string>

namespace SpDiag {

constexpr const char* defaultShmName = "/spdiag_shm_default";
constexpr const char* shmPathEnvironment = "SPDIAG_SHM_PATH";

bool NormalizeShmPath(const std::string& input, std::string& shmName,
                      std::string& error);
bool ResolveSdkShmName(const std::string& configured, std::string& shmName,
                       std::string& error);
std::string ShmFilesystemPath(const std::string& shmName);
std::string ShmScopedDataDir(const std::string& baseDir,
                             const std::string& shmName, uint32_t uid);

}
