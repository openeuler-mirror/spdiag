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

#include "symbol_resolver.h"

#include <fstream>
#include <sstream>

namespace SpDiag {

bool SymbolResolver::Resolve(int32_t pid, uint64_t address, SymbolInfo& out) {
    if (NeedsRefresh(pid)) {
        if (!RefreshMaps(pid)) {
            return false;
        }
    }

    const MapEntry* entry = FindMapEntry(cachedMaps_, address);
    if (entry == nullptr) {
        return false;
    }

    out.address = address;
    out.filePath = entry->filePath;
    out.symbolName = "<unresolved>";
    out.line = 0;
    return true;
}

std::vector<SymbolInfo> SymbolResolver::ResolveStack(int32_t pid, const std::vector<uint64_t>& addresses) {
    std::vector<SymbolInfo> symbols;
    symbols.reserve(addresses.size());
    for (uint64_t address : addresses) {
        SymbolInfo info;
        if (Resolve(pid, address, info)) {
            symbols.push_back(std::move(info));
        }
    }
    return symbols;
}

bool SymbolResolver::RefreshMaps(int32_t pid) {
    std::vector<MapEntry> maps;
    if (!LoadMaps(pid, maps)) {
        return false;
    }

    cachedPid_ = pid;
    cachedMaps_ = std::move(maps);
    lastRefresh_ = std::chrono::steady_clock::now();
    return true;
}

void SymbolResolver::SetRefreshInterval(std::chrono::seconds interval) {
    refreshInterval_ = interval;
}

const std::vector<SymbolResolver::MapEntry>& SymbolResolver::GetCachedMaps(int32_t pid) {
    if (NeedsRefresh(pid)) {
        RefreshMaps(pid);
    }
    return cachedMaps_;
}

bool SymbolResolver::NeedsRefresh(int32_t pid) const {
    if (pid != cachedPid_) {
        return true;
    }
    if (cachedMaps_.empty()) {
        return true;
    }

    const auto now = std::chrono::steady_clock::now();
    return (now - lastRefresh_) >= refreshInterval_;
}

bool SymbolResolver::LoadMaps(int32_t pid, std::vector<MapEntry>& entries) const {
    std::stringstream path;
    path << "/proc/" << pid << "/maps";

    std::ifstream mapsFile(path.str());
    if (!mapsFile.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(mapsFile, line)) {
        std::istringstream lineStream(line);
        std::string addrRange;
        std::string perms;
        std::string offsetHex;
        std::string dev;
        std::string inode;
        std::string pathName;

        lineStream >> addrRange >> perms >> offsetHex >> dev >> inode;
        std::getline(lineStream, pathName);

        const auto dashPos = addrRange.find('-');
        if (dashPos == std::string::npos) {
            continue;
        }

        MapEntry entry;
        entry.start = std::stoull(addrRange.substr(0, dashPos), nullptr, 16);
        entry.end = std::stoull(addrRange.substr(dashPos + 1), nullptr, 16);
        entry.fileOffset = std::stoull(offsetHex, nullptr, 16);

        while (!pathName.empty() && pathName.front() == ' ') {
            pathName.erase(pathName.begin());
        }
        entry.filePath = pathName;
        entries.push_back(std::move(entry));
    }

    return true;

}

const SymbolResolver::MapEntry* SymbolResolver::FindMapEntry(const std::vector<MapEntry>& entries, uint64_t address) const {
    for (const auto& entry : entries) {
        if (address >= entry.start && address < entry.end) {
            return &entry;
        }
    }
    return nullptr;
}

}
