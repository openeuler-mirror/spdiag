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

#include "sort_spec.h"
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace SpDiag {
namespace Cli {

namespace {

struct KeyInfo {
    const char* name;
    SortKey::Key key;
    bool defaultDesc;
};

// clang-format off
static const KeyInfo kMemstatKeys[] = {
    {"current",     SortKey::CURRENT,    true},
    {"peak",        SortKey::PEAK,       true},
    {"total",       SortKey::TOTAL,      true},
    {"allocs",      SortKey::ALLOCS,     true},
    {"frees",       SortKey::FREES,      true},
    {"max_size",    SortKey::MAX_SIZE,   true},
    {"min_size",    SortKey::MIN_SIZE,   true},
    {"first_seen",  SortKey::FIRST_SEEN, false},
    {"leaked",      SortKey::LEAKED,     true},
    {"avg_size",    SortKey::AVG_SIZE,   true},
    {"frame",       SortKey::FRAME,      false},
};

static const KeyInfo kPerfPointKeys[] = {
    {"name",    SortKey::NAME,     false},
    {"lvl",     SortKey::LVL,      false},
    {"ticks",   SortKey::TICKS,    true},
    {"good",    SortKey::GOOD,     true},
    {"bad",     SortKey::BAD,      true},
    {"total",   SortKey::TOTAL_TIME, true},
    {"avg",     SortKey::AVG_TIME, true},
    {"min",     SortKey::MIN_TIME, true},
    {"max",     SortKey::MAX_TIME, true},
};
// clang-format on

static const KeyInfo* FindKey(const std::string& name, const std::string& mode) {
    if (mode == "memstat" || mode == "mempoint") {
        for (const auto& ki : kMemstatKeys) {
            if (name == ki.name) return &ki;
        }
    } else {
        for (const auto& ki : kPerfPointKeys) {
            if (name == ki.name) return &ki;
        }
    }
    return nullptr;
}

}  // namespace

std::vector<SortKey> ParseSortSpec(const std::string& spec,
                                   const std::string& mode,
                                   std::string& errorMsg) {
    std::vector<SortKey> result;
    size_t pos = 0;
    while (pos < spec.size()) {
        size_t comma = spec.find(',', pos);
        std::string token = (comma == std::string::npos)
            ? spec.substr(pos)
            : spec.substr(pos, comma - pos);
        pos = (comma == std::string::npos) ? spec.size() : comma + 1;

        // Trim whitespace
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front())))
            token.erase(token.begin());
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back())))
            token.pop_back();

        if (token.empty()) continue;

        // Split KEY:DIR
        std::string keyName = token;
        bool desc = true;  // will be overridden by key default
        size_t colon = token.rfind(':');
        if (colon != std::string::npos && colon > 0) {
            std::string dir = token.substr(colon + 1);
            keyName = token.substr(0, colon);
            if (dir == "asc") {
                desc = false;
            } else if (dir == "desc") {
                desc = true;
            } else {
                errorMsg = "Invalid sort direction '" + dir + "' for key '" + keyName +
                           "' (use asc or desc)";
                return {};
            }
        } else {
            // No direction specified — use key default
            const KeyInfo* ki = FindKey(keyName, mode);
            if (ki) {
                desc = ki->defaultDesc;
            }
        }

        const KeyInfo* ki = FindKey(keyName, mode);
        if (!ki) {
            errorMsg = "Unknown sort key '" + keyName + "' for " + mode;
            return {};
        }

        result.push_back({ki->key, desc});
    }
    return result;
}

const std::vector<SortKey>& DefaultMemstatSortKeys() {
    static const std::vector<SortKey> keys = {
        {SortKey::CURRENT, true},
        {SortKey::PEAK,    true},
        {SortKey::ALLOCS,  true},
    };
    return keys;
}

const std::vector<SortKey>& DefaultMemPointSortKeys() {
    static const std::vector<SortKey> keys = {
        {SortKey::CURRENT, true},
        {SortKey::PEAK,    true},
    };
    return keys;
}

const std::vector<SortKey>& DefaultPerfPointSortKeys() {
    static const std::vector<SortKey> keys = {
        {SortKey::NAME, false},
    };
    return keys;
}

}  // namespace Cli
}  // namespace SpDiag
