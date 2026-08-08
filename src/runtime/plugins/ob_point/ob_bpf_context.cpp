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

// See ob_bpf_context.h for design notes. The whole translation unit is guarded
// the same way as the header: only built on Linux with BPF in use.

#ifdef SPDIAG_USES_BPF

#include "ob_bpf_context.h"

#include "ebpf/bpf_loader.h"
#include "spdiag/log_macros.h"

namespace SpDiag {

ObBpfContext::ObBpfContext(pid_t pid, std::string libcPath)
    : pid_(pid), libcPath_(std::move(libcPath)) {}

ObBpfContext::~ObBpfContext() {
    std::lock_guard<std::mutex> guard(mtx_);
    if (loader_) {
        loader_->DetachAndUnload();
        loader_.reset();
    }
    loaded_ = false;
}

bool ObBpfContext::EnsureLoaded() {
    if (loaded_) return true;
    loader_ = std::make_unique<BpfLoader>();
    if (!loader_->LoadAndAttach(pid_, libcPath_)) {
        SPDIAG_LOG_ERROR(LogModule::PLATFORM,
                         "ObBpfContext LoadAndAttach failed (pid=%d): %s",
                         pid_, loader_->GetLastError().c_str());
        loader_.reset();
        return false;
    }
    loaded_ = true;
    return true;
}

void ObBpfContext::MaybeUnload() {
    if (mallocRefs_ == 0 && mempointRefs_ == 0 && loader_) {
        loader_->DetachAndUnload();
        loader_.reset();
        loaded_ = false;
    }
}

bool ObBpfContext::Load() {
    std::lock_guard<std::mutex> guard(mtx_);
    return EnsureLoaded();
}

bool ObBpfContext::AcquireMallocUprobes() {
    std::lock_guard<std::mutex> guard(mtx_);
    if (!EnsureLoaded()) return false;
    ++mallocRefs_;
    return true;
}

void ObBpfContext::ReleaseMallocUprobes() {
    std::lock_guard<std::mutex> guard(mtx_);
    if (mallocRefs_ > 0) --mallocRefs_;
    MaybeUnload();
}

bool ObBpfContext::AcquireMempointUsdts() {
    std::lock_guard<std::mutex> guard(mtx_);
    if (!EnsureLoaded()) return false;
    ++mempointRefs_;
    return true;
}

void ObBpfContext::ReleaseMempointUsdts() {
    std::lock_guard<std::mutex> guard(mtx_);
    if (mempointRefs_ > 0) --mempointRefs_;
    MaybeUnload();
}

struct bpf_object* ObBpfContext::GetBpfObject() const {
    std::lock_guard<std::mutex> guard(mtx_);
    if (!loader_) return nullptr;
    return loader_->GetBpfObject();
}

int ObBpfContext::GetMapFd(const std::string& name) const {
    std::lock_guard<std::mutex> guard(mtx_);
    if (!loader_) return -1;
    return loader_->GetMapFd(name);
}

}  // namespace SpDiag

#endif
