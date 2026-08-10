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
#include <vector>
#include <string>
#include <cstdint>
#include <fstream>

namespace SpDiag {

struct DumpStat {
    uint32_t globalSlotIndex = 0;
    std::string programName;
    std::string moduleName;
    std::string pointName;
    uint8_t level = 0;
    uint64_t deltaTickCount = 0;
    uint64_t deltaGoodCount = 0;
    uint64_t deltaBadCount = 0;
    uint64_t deltaNotCount = 0;
    uint64_t deltaTimeNs = 0;
    uint64_t minTimeNs = 0;
    uint64_t maxTimeNs = 0;
#ifdef SPDIAG_ENABLE_PERCENTILE
    std::vector<uint64_t> topN;
    uint64_t p99Ns = 0;
    uint64_t p999Ns = 0;
    uint64_t p9999Ns = 0;
    bool hasP99 = false;
    bool hasP999 = false;
    bool hasP9999 = false;
#endif
};

struct DumpRecord {
    uint64_t timestampNs = 0;
    std::vector<DumpStat> stats;
};

class DumpRecorder {
public:
    explicit DumpRecorder(uint32_t capacity = 30);

    void Record(const DumpRecord& record);

    size_t Count() const;
    const DumpRecord& GetRecord(size_t index) const;

    bool SaveToFile(const std::string& filePath) const;
    bool LoadFromFile(const std::string& filePath);

    uint32_t Capacity() const;
    void SetCapacity(uint32_t newCapacity);

private:
    uint32_t capacity_;
    uint32_t count_ = 0;
    uint32_t head_ = 0;
    std::vector<DumpRecord> records_;

    size_t PhysicalIndex(size_t logicalIndex) const;
};

}
