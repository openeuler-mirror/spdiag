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
#include <string>
#include <vector>
#include <cstdint>

namespace SpDiag {
namespace Cli {

struct SortKey {
    enum Key {
        // Memstat keys
        CURRENT, PEAK, TOTAL, ALLOCS, FREES,
        MAX_SIZE, MIN_SIZE, FIRST_SEEN, LEAKED, AVG_SIZE, FRAME,
        // PerfPoint keys
        NAME, LVL, TICKS, GOOD, BAD, AVG_TIME, MIN_TIME, MAX_TIME, TOTAL_TIME
    };
    Key  key;
    bool desc;  // true = descending, false = ascending
};

// Parse "KEY[:DIR][,KEY[:DIR]]..." into SortKey vector.
// mode = "memstat" or "perfpoint" to validate relevant keys.
// Returns parsed keys, or empty + sets errorMsg on failure.
std::vector<SortKey> ParseSortSpec(const std::string& spec,
                                   const std::string& mode,
                                   std::string& errorMsg);

// Default sort chains
const std::vector<SortKey>& DefaultMemstatSortKeys();
const std::vector<SortKey>& DefaultMemPointSortKeys();
const std::vector<SortKey>& DefaultPerfPointSortKeys();

}  // namespace Cli
}  // namespace SpDiag
