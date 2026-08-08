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
#include "platform.h"
#include "spdiag/shm_layout.h"

namespace SpDiag {

struct ShmProbeResult {
    bool valid = false;
    size_t totalSize = 0;
};

inline ShmProbeResult ProbeShmHeader(const std::string& name) {
    ShmProbeResult result;
    ShmHandle probeHandle = Platform::ShmOpenReadOnly(name, sizeof(ShmHeader));
    if (probeHandle.baseAddr == nullptr) {
        return result;
    }
    const ShmHeader* hdr = reinterpret_cast<const ShmHeader*>(probeHandle.baseAddr);
    result.valid = (hdr->magic == shmMagic && hdr->shmTotalSize > 0);
    result.totalSize = hdr->shmTotalSize;
    Platform::ShmClose(probeHandle);
    return result;
}

}
