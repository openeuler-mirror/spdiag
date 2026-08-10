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

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace SpDiag {

struct SymbolInfo {
    std::string symbolName;
    std::string filePath;
    uint32_t line = 0;
    uint64_t address = 0;
};

class SymbolResolver {
public:
    SymbolResolver() = default;

    bool Resolve(int32_t pid, uint64_t address, SymbolInfo& out);
    std::vector<SymbolInfo> ResolveStack(int32_t pid, const std::vector<uint64_t>& addresses);
    bool RefreshMaps(int32_t pid);
    void SetRefreshInterval(std::chrono::seconds interval);

    struct MapEntry {
        uint64_t start = 0;
        uint64_t end = 0;
        uint64_t fileOffset = 0;
        std::string filePath;
    };

    const std::vector<MapEntry>& GetCachedMaps(int32_t pid);

private:

    bool NeedsRefresh(int32_t pid) const;
    bool LoadMaps(int32_t pid, std::vector<MapEntry>& entries) const;
    const MapEntry* FindMapEntry(const std::vector<MapEntry>& entries, uint64_t address) const;

    int32_t cachedPid_ = -1;
    std::vector<MapEntry> cachedMaps_;
    std::chrono::steady_clock::time_point lastRefresh_{};
    std::chrono::seconds refreshInterval_{10};
};

}
