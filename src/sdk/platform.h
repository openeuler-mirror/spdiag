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
#include <string>
#include <cstdint>
#include <cstddef>

namespace SpDiag {

struct ShmHandle {
    void*    baseAddr   = nullptr;
    size_t   mappedSize = 0;
    int      fd         = -1;
};

namespace Platform {

ShmHandle ShmCreate(const std::string& name, size_t size);
ShmHandle ShmOpenReadOnly(const std::string& name, size_t size);
ShmHandle ShmOpenReadWrite(const std::string& name, size_t size);
void      ShmClose(ShmHandle& handle);
void      ShmDestroy(const std::string& name);

uint32_t  GetCurrentCpuId();
uint64_t  GetTimestampNs();
uint64_t  CalibrateTimestamp();

}

}
