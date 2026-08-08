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

#ifdef SPDIAG_ENABLE_MEMPOINT


#include <cstdint>
#include <string>
#include <vector>
#include <sys/types.h>

#include "data_sink.h"

struct bpf_object;
struct bpf_link;

namespace SpDiag {

class PointMemProfiler {
public:
    PointMemProfiler();
    ~PointMemProfiler();

    PointMemProfiler(const PointMemProfiler&) = delete;
    PointMemProfiler& operator=(const PointMemProfiler&) = delete;

    bool Attach(struct bpf_object* obj, pid_t pid, const std::string& binaryPath);
    bool Collect(DataSink* sink);
    void Detach();

private:
    void CollectStatMap(DataSink* sink);
    void CollectLeakInfo(DataSink* sink);
    void CollectStructMap(DataSink* sink);

    static const char* KeyName(uint32_t id);

    int statMapFd_ = -1;
    int liveObjsFd_ = -1;
    int structMapFd_ = -1;
    pid_t targetPid_ = -1;
    bool attached_ = false;

    struct bpf_link* enterLink_ = nullptr;
    struct bpf_link* leaveLink_ = nullptr;
    struct bpf_link* resizeLink_ = nullptr;
    struct bpf_link* structSnapshotLink_ = nullptr;
};

} // namespace SpDiag

#endif // SPDIAG_ENABLE_MEMPOINT
