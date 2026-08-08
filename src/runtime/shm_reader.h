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
#include <vector>
#include "sdk/platform.h"
#include "spdiag/shm_layout.h"

namespace SpDiag {

class ShmReader {
public:
    ShmReader() = default;
    ~ShmReader();

    bool Open(const std::string& shmName);
    void Close();

    const ShmHeader& Header() const;
    const ProgramEntry* ReadProgramRegistry() const;
    const char (*ReadKeyNames() const)[nameLen];
    const char (*ReadKeyModuleNames() const)[nameLen];
    // 全局 PerfPoint 累加区（v3）；缺省(globalAccumCount==0)返回 nullptr。直接指针读取，不做快照校验。
    const PerfStat* ReadGlobalAccum() const;
    // globalOut（可选）非空时，在同一 dumpSeqNo 偶/奇窗口内一并快照全局区
    // （globalAccumCount * sizeof(PerfStat) 原始字节，无 coreId 前缀）。
    bool SnapshotShards(ShardBuffer& out, std::vector<uint8_t>* globalOut = nullptr) const;

private:
    ShmHandle handle_;
    bool opened_ = false;
};

}
