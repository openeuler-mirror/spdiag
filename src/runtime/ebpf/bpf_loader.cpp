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

#include "bpf_loader.h"
#include "ob_point.skel.h"

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <cerrno>
#include <cstring>
#include <unistd.h>

namespace SpDiag {

static std::string RunCommand(const std::string& cmd) {
    std::string result;
    char buf[256];
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return result;
    while (fgets(buf, sizeof(buf), pipe)) {
        result += buf;
    }
    pclose(pipe);
    return result;
}

BpfLoader::BpfLoader() = default;

BpfLoader::~BpfLoader() {
    DetachAndUnload();
}

bool BpfLoader::ResolveLibcSymbols(const std::string& libcPath) {
    std::string cmd = "nm -D " + libcPath + " 2>/dev/null";
    std::string output = RunCommand(cmd);

    const char* targets[] = {"malloc", "free", "calloc", "realloc"};
    for (const char* name : targets) {
        std::string prefix = std::string(" ") + name;
        size_t pos = 0;
        bool found = false;
        while ((pos = output.find(prefix, pos)) != std::string::npos) {
            size_t line_start = (pos > 0) ? output.rfind('\n', pos - 1) : 0;
            if (line_start == std::string::npos) line_start = 0;
            size_t line_end = output.find('\n', pos);
            if (line_end == std::string::npos) line_end = output.size();

            std::string line = output.substr(
                line_start + (line_start > 0 ? 1 : 0),
                line_end - line_start - (line_start > 0 ? 0 : 0));

            unsigned long long addr = 0;
            if (sscanf(line.c_str(), "%llx", &addr) == 1 && addr != 0) {
                SymbolOffset sym;
                sym.name = name;
                sym.offset = static_cast<uint64_t>(addr);
                symbols_.push_back(sym);
                found = true;
                break;
            }
            pos++;
        }
        if (!found) {
            lastError_ = std::string("Symbol not found: ") + name + " in " + libcPath;
            return false;
        }
    }
    return true;
}

bool BpfLoader::LoadAndAttach(pid_t pid, const std::string& libcPath) {
    targetPid_ = pid;
    libcPath_ = libcPath;

    if (!ResolveLibcSymbols(libcPath)) {
        return false;
    }

    skel_ = ob_point_bpf__open();
    if (!skel_) {
        lastError_ = "Failed to open BPF skeleton";
        return false;
    }

    if (ob_point_bpf__load(skel_) != 0) {
        lastError_ = "Failed to load BPF object";
        ob_point_bpf__destroy(skel_);
        skel_ = nullptr;
        return false;
    }

    struct AttachPoint {
        const char* progName;
        const char* symName;
        bool isReturn;
    };

    AttachPoint attachPoints[] = {
        {"on_malloc_enter",  "malloc",  false},
        {"on_alloc_exit",    "malloc",  true},
        {"on_calloc_enter",  "calloc",  false},
        {"on_realloc_enter", "realloc", false},
        {"on_free_enter",    "free",    false},
    };

    for (auto& ap : attachPoints) {
        struct bpf_program* prog = bpf_object__find_program_by_name(skel_->obj, ap.progName);
        if (!prog) {
            lastError_ = std::string("Program not found: ") + ap.progName;
            DetachAndUnload();
            return false;
        }

        uint64_t offset = 0;
        for (auto& sym : symbols_) {
            if (sym.name == ap.symName) {
                offset = sym.offset;
                break;
            }
        }
        if (offset == 0) {
            lastError_ = std::string("Offset not resolved for: ") + ap.symName;
            DetachAndUnload();
            return false;
        }

        bpf_uprobe_opts opts{};
        opts.sz = sizeof(opts);
        opts.retprobe = ap.isReturn;

        struct bpf_link* link = bpf_program__attach_uprobe_opts(
            prog, targetPid_, libcPath_.c_str(), offset, &opts);
        if (!link) {
            lastError_ = std::string("Attach failed for ") + ap.progName + ": " + std::strerror(errno);
            DetachAndUnload();
            return false;
        }
        links_.push_back(link);
    }

    return true;
}

void BpfLoader::DetachAndUnload() {
    for (auto* link : links_) {
        bpf_link__destroy(link);
    }
    links_.clear();

    if (skel_) {
        ob_point_bpf__destroy(skel_);
        skel_ = nullptr;
    }
}

int BpfLoader::GetMapFd(const std::string& mapName) const {
    if (!skel_) return -1;
    struct bpf_map* map = bpf_object__find_map_by_name(skel_->obj, mapName.c_str());
    if (!map) return -1;
    return bpf_map__fd(map);
}

struct bpf_object* BpfLoader::GetBpfObject() const {
    if (!skel_) return nullptr;
    return skel_->obj;
}

const std::string& BpfLoader::GetLastError() const {
    return lastError_;
}

}
