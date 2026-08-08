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
#include "spdiag/shm_layout.h"
#include "platform.h"

namespace SpDiag {

class ShmWriter {
public:
    ShmWriter() = default;
    ~ShmWriter();

    bool Create(const std::string& shmName, size_t totalSize);
    bool Open(const std::string& shmName, size_t totalSize);
    void Destroy();
    void Release();

    ShmHeader*   Header();
    void*        ShardPtr(uint32_t programIndex, uint32_t coreIndex);
    PerfStat*    GlobalAccumPtr();   // 全局 PerfPoint 累加区（v3）；缺省(globalAccumCount==0)返回 nullptr

    void WriteHeader(const ShmHeader& hdr);
    void WriteShard(uint32_t programIndex, uint32_t coreIndex, const void* data, size_t size);

    size_t TotalSize() const;

private:
    ShmHandle   handle_;
    std::string shmName_;
    size_t      totalSize_ = 0;
    bool        owner_     = false;
};

}
