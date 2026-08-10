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

#ifdef SPDIAG_ENABLE_OB_MEMORY


#include "memory_profiler.h"

#include <bpf/bpf.h>
#include <cxxabi.h>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <fstream>
#include <chrono>

#ifdef SPDIAG_DEBUG_FRAMES
#include <iostream>
#endif

namespace SpDiag {

static std::string Demangle(const std::string& mangled) {
    if (mangled.empty() || mangled[0] != '_') return mangled;
    int status = 0;
    char* demangled = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);
    if (status == 0 && demangled) {
        std::string result(demangled);
        std::free(demangled);
        return result;
    }
    if (demangled) std::free(demangled);
    return mangled;
}

MemoryProfiler::MemoryProfiler() = default;

MemoryProfiler::~MemoryProfiler() {
    Detach();
}

bool MemoryProfiler::Attach(pid_t pid, int aggFd, int stacksFd) {
    targetPid_ = pid;
    symbolResolver_.SetRefreshInterval(std::chrono::seconds(300));
    aggFd_ = aggFd;
    stacksFd_ = stacksFd;
    if (aggFd_ < 0 || stacksFd_ < 0) {
        attached_ = false;
        return false;
    }
    attached_ = true;
    return true;
}

bool MemoryProfiler::Collect(DataSink* sink) {
    if (!attached_ || !sink) {
        return false;
    }

    agg_key key = {};
    agg_key nextKey = {};

    while (bpf_map_get_next_key(aggFd_, &key, &nextKey) == 0) {
        agg_val val = {};
        if (bpf_map_lookup_elem(aggFd_, &nextKey, &val) == 0) {
            std::string stackStr = FormatStack(nextKey.stack_id);
            std::vector<FrameInfo> frames = FormatStackFrames(nextKey.stack_id);
            std::string prefix = "mem.pid." + std::to_string(nextKey.pid) +
                                 ".tid." + std::to_string(nextKey.tid) +
                                 ".stack." + std::to_string(nextKey.stack_id);

            std::vector<std::pair<std::string, std::string>> items = {
                {prefix + ".current_bytes", std::to_string(val.current_bytes)},
                {prefix + ".total_alloc_bytes", std::to_string(val.total_alloc)},
                {prefix + ".alloc_count", std::to_string(val.alloc_count)},
                {prefix + ".free_count", std::to_string(val.free_count)},
                {prefix + ".peak_bytes", std::to_string(val.peak)},
                {prefix + ".max_size", std::to_string(val.max_size)},
                {prefix + ".min_size", std::to_string(val.min_size)},
                {prefix + ".first_seen_ns", std::to_string(val.first_seen_ns)},
                {prefix + ".last_seen_ns", std::to_string(val.last_seen_ns)},
                {prefix + ".stack_trace", stackStr},
            };

            for (size_t i = 0; i < frames.size(); ++i) {
                std::string framePrefix = prefix + ".frame." + std::to_string(i);
                items.push_back({framePrefix + ".func", frames[i].func});
                items.push_back({framePrefix + ".module", frames[i].module});
                if (!frames[i].file.empty()) {
                    items.push_back({framePrefix + ".file", frames[i].file});
                    items.push_back({framePrefix + ".line", std::to_string(frames[i].line)});
                }
            }

            sink->SubmitBatch(items);
        }
        key = nextKey;
    }

    return true;
}

void MemoryProfiler::Detach() {
    // BPF object ownership belongs to ObBpfContext; we only drop our map fds here.
    aggFd_ = -1;
    stacksFd_ = -1;
    attached_ = false;
}

std::vector<FrameInfo> MemoryProfiler::FormatStackFrames(int32_t stackId) {
    std::vector<FrameInfo> frames;
    if (stackId < 0) {
        return frames;
    }

    uint64_t ips[64] = {};
    if (bpf_map_lookup_elem(stacksFd_, &stackId, ips) != 0) {
        return frames;
    }

    for (int i = 0; i < 64 && ips[i] != 0; ++i) {
        frames.push_back(ResolveFrame(ips[i]));
    }
    return frames;
}

std::string MemoryProfiler::FormatStack(int32_t stackId) {
    std::vector<FrameInfo> frames = FormatStackFrames(stackId);
    if (frames.empty()) {
        return stackId < 0 ? "[unknown]" : "[lookup_failed]";
    }

    std::ostringstream oss;
    for (size_t i = 0; i < frames.size(); ++i) {
        if (i > 0) oss << " <- ";
        oss << frames[i].func;
        if (!frames[i].file.empty() && frames[i].line > 0) {
            oss << " @ " << frames[i].file << ":" << frames[i].line;
        }
    }
    return oss.str();
}

FrameInfo MemoryProfiler::ResolveFrame(uint64_t ip) {
    FrameInfo fi;
    fi.ip = ip;

    const auto& maps = symbolResolver_.GetCachedMaps(targetPid_);

#ifdef SPDIAG_DEBUG_FRAMES
    std::cerr << "[FRAME-DEBUG] ip=0x" << std::hex << ip << std::dec << "\n";
    std::cerr << "[FRAME-DEBUG]   maps_count=" << maps.size() << "\n";
#endif

    for (const auto& entry : maps) {
        if (ip < entry.start || ip >= entry.end) continue;
        if (entry.filePath.empty()) continue;

        uint64_t fileOff = ip - entry.start + entry.fileOffset;

#ifdef SPDIAG_DEBUG_FRAMES
        std::cerr << "[FRAME-DEBUG]   matched_map: start=0x" << std::hex << entry.start
                  << " end=0x" << entry.end
                  << " fileOff_in_map=0x" << entry.fileOffset
                  << " path='" << entry.filePath << "'\n";
        std::cerr << "[FRAME-DEBUG]   fileOff=0x" << fileOff << std::dec << "\n";
#endif

        size_t slash = entry.filePath.rfind('/');
        fi.module = (slash != std::string::npos)
            ? entry.filePath.substr(slash + 1) : entry.filePath;

        const ElfSymbolTable* tab = ElfSymbolTableLoader::Load(entry.filePath);

#ifdef SPDIAG_DEBUG_FRAMES
        std::cerr << "[FRAME-DEBUG]   elf_loaded=" << (tab != nullptr)
                  << " symbols_count=" << (tab ? tab->symbols.size() : 0)
                  << " vaddrBias=0x" << std::hex << (tab ? tab->vaddrBias : 0) << std::dec << "\n";
#endif

        if (tab && !tab->symbols.empty()) {
            uint64_t vaddr = fileOff + tab->vaddrBias;

#ifdef SPDIAG_DEBUG_FRAMES
            std::cerr << "[FRAME-DEBUG]   vaddr=0x" << std::hex << vaddr << std::dec << "\n";
            std::cerr << "[FRAME-DEBUG]   sym_first=0x" << std::hex << tab->symbols.front().addr
                      << " sym_last=0x" << tab->symbols.back().addr << std::dec << "\n";
#endif

            std::string name = ElfSymbolTableLoader::FindSymbol(tab, vaddr);

#ifdef SPDIAG_DEBUG_FRAMES
            std::cerr << "[FRAME-DEBUG]   find_result='" << name << "'\n";
#endif

            if (!name.empty()) {
                fi.func = Demangle(name);
                LookupSourceLocation(entry.filePath, vaddr, fi.file, fi.line);
                return fi;
            }
        }

        std::ostringstream sym;
        sym << fi.module << "+0x" << std::hex << fileOff;
        fi.func = sym.str();

#ifdef SPDIAG_DEBUG_FRAMES
        std::cerr << "[FRAME-DEBUG]   fallback='" << fi.func << "'\n";
#endif

        return fi;
    }

    std::ostringstream oss;
    oss << "0x" << std::hex << ip;
    fi.func = oss.str();

#ifdef SPDIAG_DEBUG_FRAMES
    std::cerr << "[FRAME-DEBUG]   no_map_matched='" << fi.func << "'\n";
#endif

    return fi;
}

void MemoryProfiler::EnsureDwarfCache(const std::string& filePath) {
    if (dwarfCache_.find(filePath) != dwarfCache_.end()) return;
    auto dwarf = std::make_unique<DwarfDebugLine>();
    if (dwarf->Load(filePath)) {
        dwarfCache_[filePath] = std::move(dwarf);
    } else {
        dwarfCache_[filePath] = nullptr;
    }
}

void MemoryProfiler::LookupSourceLocation(const std::string& filePath, uint64_t vaddr,
                                          std::string& outFile, uint32_t& outLine) {
    EnsureDwarfCache(filePath);
    auto it = dwarfCache_.find(filePath);
    if (it == dwarfCache_.end() || !it->second) return;
    SourceLocation loc = it->second->Lookup(vaddr);
    if (loc.line > 0) {
        outFile = loc.filePath;
        outLine = loc.line;
    }
}

}

#endif // SPDIAG_ENABLE_OB_MEMORY
