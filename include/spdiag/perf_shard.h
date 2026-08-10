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
#include <memory>
#include <cstdint>
#include "spdiag/shm_layout.h"

namespace SpDiag {

class PerfShardManager {
public:
    void Init(uint32_t numCores);
    LocalPerfShard& GetShard();
    LocalPerfShard* GetShardArray();
    uint32_t GetNumCores() const;
    void SnapshotAndReset(LocalPerfShard& snapshot, LocalPerfShard& active);

private:
    std::unique_ptr<LocalPerfShard[]> shards_;
    uint32_t numCores_ = 0;
};

}
