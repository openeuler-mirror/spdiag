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

#include <cstdint>
#include <string>
#include <vector>
#include <sys/types.h>

struct ob_point_bpf;
struct bpf_object;
struct bpf_link;

namespace SpDiag {

class BpfLoader {
public:
    BpfLoader();
    ~BpfLoader();

    bool LoadAndAttach(pid_t pid, const std::string& libcPath);
    void DetachAndUnload();

    int GetMapFd(const std::string& mapName) const;
    struct bpf_object* GetBpfObject() const;
    const std::string& GetLastError() const;

private:
    bool ResolveLibcSymbols(const std::string& libcPath);

    struct ob_point_bpf* skel_ = nullptr;
    std::vector<struct bpf_link*> links_;
    std::string lastError_;
    std::string libcPath_;
    pid_t targetPid_ = -1;

    struct SymbolOffset {
        std::string name;
        uint64_t offset;
    };
    std::vector<SymbolOffset> symbols_;
};

}
