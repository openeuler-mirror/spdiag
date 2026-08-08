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

#ifdef SPDIAG_ENABLE_OB_MEMORY


#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <sys/types.h>

#include "data_sink.h"
#include "dwarf_parser.h"
#include "elf_symbol_table.h"
#include "ob_point_types.h"
#include "symbol_resolver.h"

namespace SpDiag {

class MemoryProfiler {
public:
    MemoryProfiler();
    ~MemoryProfiler();

    MemoryProfiler(const MemoryProfiler&) = delete;
    MemoryProfiler& operator=(const MemoryProfiler&) = delete;

    // BPF object ownership now lives in ObBpfContext; the profiler only consumes
    // the agg/stacks map fds it is handed. Detach() no longer unloads anything.
    bool Attach(pid_t pid, int aggFd, int stacksFd);
    bool Collect(DataSink* sink);
    void Detach();

private:
    std::vector<FrameInfo> FormatStackFrames(int32_t stackId);
    std::string FormatStack(int32_t stackId);
    FrameInfo ResolveFrame(uint64_t ip);
    void EnsureDwarfCache(const std::string& filePath);
    void LookupSourceLocation(const std::string& filePath, uint64_t vaddr,
                              std::string& outFile, uint32_t& outLine);

    SymbolResolver symbolResolver_;
    std::unordered_map<std::string, std::unique_ptr<DwarfDebugLine>> dwarfCache_;
    int aggFd_ = -1;
    int stacksFd_ = -1;
    pid_t targetPid_ = -1;
    bool attached_ = false;
};

}

#endif // SPDIAG_ENABLE_OB_MEMORY
