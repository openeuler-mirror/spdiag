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

// ObBpfContext owns the single shared ob_point BPF object (one skeleton covers
// both the libc malloc uprobes used by memstat and the ob_mempoint USDT
// programs used by mempoint; their maps are fully independent). It is held by
// SpDiagManager and injected into the memstat / mempoint plugins, replacing the
// former implicit dependency where PointMemProfiler had to borrow the object
// from MemoryProfiler.
//
// Lifetime: lazy-loaded on the first Acquire*; unloaded only when both ref
// counts drop to zero (or on destruction). Plugins call Acquire/Release in
// Start/Stop pairs and never load/unload the skeleton themselves.

#ifdef SPDIAG_USES_BPF

#include <memory>
#include <mutex>
#include <string>
#include <sys/types.h>

struct bpf_object;

namespace SpDiag {

class BpfLoader;

class ObBpfContext {
public:
    ObBpfContext(pid_t pid, std::string libcPath);
    ~ObBpfContext();

    ObBpfContext(const ObBpfContext&) = delete;
    ObBpfContext& operator=(const ObBpfContext&) = delete;

    // Explicit, idempotent load of the shared ob_point BPF skeleton. Called by
    // SpDiagManager at StartObCollect time so BPF load failures surface before
    // any plugin starts. Acquire*() lazy-load as a backstop. The skeleton is
    // owned solely here (single ob_point_bpf__open/load in the process);
    // memstat/mempoint access maps/programs via GetMapFd / GetBpfObject and
    // never load it themselves.
    bool Load();

    // memstat path: ensures the skeleton is loaded (which also attaches the
    // malloc uprobes, as BpfLoader::LoadAndAttach is atomic). Idempotent.
    bool AcquireMallocUprobes();
    void ReleaseMallocUprobes();

    // mempoint path: ensures the skeleton is loaded so mp_* maps and the USDT
    // programs exist. The USDT attach itself is done by PointMemProfiler.
    bool AcquireMempointUsdts();
    void ReleaseMempointUsdts();

    // Accessors (return nullptr/-1 if not loaded). PointMemProfiler::Attach uses
    // GetBpfObject(); memstat uses GetMapFd("agg"/"stacks").
    struct bpf_object* GetBpfObject() const;
    int GetMapFd(const std::string& name) const;

private:
    bool EnsureLoaded();   // idempotent; BpfLoader::LoadAndAttach(pid_, libcPath_)
    void MaybeUnload();    // both refs == 0 -> DetachAndUnload

    mutable std::mutex mtx_;
    std::unique_ptr<BpfLoader> loader_;
    int mallocRefs_ = 0;
    int mempointRefs_ = 0;
    bool loaded_ = false;
    pid_t pid_;
    std::string libcPath_;
};

}  // namespace SpDiag

#endif
