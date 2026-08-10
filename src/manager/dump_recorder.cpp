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

#include "dump_recorder.h"
#include <stdexcept>
#include <algorithm>

namespace SpDiag {

DumpRecorder::DumpRecorder(uint32_t capacity)
    : capacity_(capacity) {
    records_.resize(capacity);
}

void DumpRecorder::Record(const DumpRecord& record) {
    records_[head_] = record;
    head_ = (head_ + 1) % capacity_;
    if (count_ < capacity_) {
        ++count_;
    }
}

size_t DumpRecorder::Count() const {
    return count_;
}

const DumpRecord& DumpRecorder::GetRecord(size_t index) const {
    if (index >= count_) {
        throw std::out_of_range("DumpRecorder::GetRecord index out of range");
    }
    return records_[PhysicalIndex(index)];
}

bool DumpRecorder::SaveToFile(const std::string& filePath) const {
    std::ofstream ofs(filePath, std::ios::binary);
    if (!ofs) return false;

    const uint32_t magic = 0x48495354;
    const uint32_t version = 1;

    ofs.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    ofs.write(reinterpret_cast<const char*>(&version), sizeof(version));
    ofs.write(reinterpret_cast<const char*>(&capacity_), sizeof(capacity_));
    ofs.write(reinterpret_cast<const char*>(&count_), sizeof(count_));
    ofs.write(reinterpret_cast<const char*>(&head_), sizeof(head_));

    size_t oldestPhysIdx = (head_ + capacity_ - count_) % capacity_;

    for (uint32_t i = 0; i < count_; ++i) {
        size_t physIdx = (oldestPhysIdx + i) % capacity_;
        const DumpRecord& rec = records_[physIdx];

        uint64_t ts = rec.timestampNs;
        ofs.write(reinterpret_cast<const char*>(&ts), sizeof(ts));

        uint32_t statCount = static_cast<uint32_t>(rec.stats.size());
        ofs.write(reinterpret_cast<const char*>(&statCount), sizeof(statCount));

        for (const DumpStat& stat : rec.stats) {
            uint32_t locId = stat.globalSlotIndex;
            ofs.write(reinterpret_cast<const char*>(&locId), sizeof(locId));

            uint64_t val;
            val = stat.deltaTickCount;
            ofs.write(reinterpret_cast<const char*>(&val), sizeof(val));
            val = stat.deltaGoodCount;
            ofs.write(reinterpret_cast<const char*>(&val), sizeof(val));
            val = stat.deltaBadCount;
            ofs.write(reinterpret_cast<const char*>(&val), sizeof(val));
            val = stat.deltaNotCount;
            ofs.write(reinterpret_cast<const char*>(&val), sizeof(val));
            val = stat.deltaTimeNs;
            ofs.write(reinterpret_cast<const char*>(&val), sizeof(val));
            val = stat.minTimeNs;
            ofs.write(reinterpret_cast<const char*>(&val), sizeof(val));
            val = stat.maxTimeNs;
            ofs.write(reinterpret_cast<const char*>(&val), sizeof(val));

#ifdef SPDIAG_ENABLE_PERCENTILE
            uint32_t topNCount = static_cast<uint32_t>(stat.topN.size());
            ofs.write(reinterpret_cast<const char*>(&topNCount), sizeof(topNCount));
            for (uint64_t v : stat.topN) {
                ofs.write(reinterpret_cast<const char*>(&v), sizeof(v));
            }

            val = stat.p99Ns;
            ofs.write(reinterpret_cast<const char*>(&val), sizeof(val));
            val = stat.p999Ns;
            ofs.write(reinterpret_cast<const char*>(&val), sizeof(val));
            val = stat.p9999Ns;
            ofs.write(reinterpret_cast<const char*>(&val), sizeof(val));

            uint8_t flags = 0;
            if (stat.hasP99) flags |= 0x01;
            if (stat.hasP999) flags |= 0x02;
            if (stat.hasP9999) flags |= 0x04;
            ofs.write(reinterpret_cast<const char*>(&flags), sizeof(flags));
#endif

            uint8_t level = stat.level;
            ofs.write(reinterpret_cast<const char*>(&level), sizeof(level));

            uint16_t modLen = static_cast<uint16_t>(stat.moduleName.size());
            ofs.write(reinterpret_cast<const char*>(&modLen), sizeof(modLen));
            if (modLen > 0) {
                ofs.write(stat.moduleName.data(), modLen);
            }

            uint16_t ptLen = static_cast<uint16_t>(stat.pointName.size());
            ofs.write(reinterpret_cast<const char*>(&ptLen), sizeof(ptLen));
            if (ptLen > 0) {
                ofs.write(stat.pointName.data(), ptLen);
            }

            uint16_t pgmLen = static_cast<uint16_t>(stat.programName.size());
            ofs.write(reinterpret_cast<const char*>(&pgmLen), sizeof(pgmLen));
            if (pgmLen > 0) {
                ofs.write(stat.programName.data(), pgmLen);
            }
        }
    }

    return ofs.good();
}

bool DumpRecorder::LoadFromFile(const std::string& filePath) {
    std::ifstream ifs(filePath, std::ios::binary);
    if (!ifs) return false;

    uint32_t magic = 0;
    ifs.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (magic != 0x48495354) return false;

    uint32_t version = 0;
    ifs.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version != 1) return false;

    uint32_t fileCapacity = 0;
    uint32_t fileCount = 0;
    uint32_t fileHead = 0;

    ifs.read(reinterpret_cast<char*>(&fileCapacity), sizeof(fileCapacity));
    ifs.read(reinterpret_cast<char*>(&fileCount), sizeof(fileCount));
    ifs.read(reinterpret_cast<char*>(&fileHead), sizeof(fileHead));

    if (!ifs) return false;

    capacity_ = fileCapacity;
    count_ = fileCount;
    head_ = fileHead;
    records_.resize(capacity_);

    size_t oldestPhysIdx = (head_ + capacity_ - count_) % capacity_;

    for (uint32_t i = 0; i < count_; ++i) {
        size_t physIdx = (oldestPhysIdx + i) % capacity_;
        DumpRecord& rec = records_[physIdx];

        uint64_t ts = 0;
        ifs.read(reinterpret_cast<char*>(&ts), sizeof(ts));
        rec.timestampNs = ts;

        uint32_t statCount = 0;
        ifs.read(reinterpret_cast<char*>(&statCount), sizeof(statCount));
        rec.stats.resize(statCount);

        for (uint32_t s = 0; s < statCount; ++s) {
            DumpStat& stat = rec.stats[s];

            uint32_t locId = 0;
            ifs.read(reinterpret_cast<char*>(&locId), sizeof(locId));
            stat.globalSlotIndex = locId;

            uint64_t val = 0;
            ifs.read(reinterpret_cast<char*>(&val), sizeof(val));
            stat.deltaTickCount = val;
            ifs.read(reinterpret_cast<char*>(&val), sizeof(val));
            stat.deltaGoodCount = val;
            ifs.read(reinterpret_cast<char*>(&val), sizeof(val));
            stat.deltaBadCount = val;
            ifs.read(reinterpret_cast<char*>(&val), sizeof(val));
            stat.deltaNotCount = val;
            ifs.read(reinterpret_cast<char*>(&val), sizeof(val));
            stat.deltaTimeNs = val;
            ifs.read(reinterpret_cast<char*>(&val), sizeof(val));
            stat.minTimeNs = val;
            ifs.read(reinterpret_cast<char*>(&val), sizeof(val));
            stat.maxTimeNs = val;

#ifdef SPDIAG_ENABLE_PERCENTILE
            uint32_t topNCount = 0;
            ifs.read(reinterpret_cast<char*>(&topNCount), sizeof(topNCount));
            stat.topN.resize(topNCount);
            for (uint32_t t = 0; t < topNCount; ++t) {
                ifs.read(reinterpret_cast<char*>(&val), sizeof(val));
                stat.topN[t] = val;
            }

            ifs.read(reinterpret_cast<char*>(&val), sizeof(val));
            stat.p99Ns = val;
            ifs.read(reinterpret_cast<char*>(&val), sizeof(val));
            stat.p999Ns = val;
            ifs.read(reinterpret_cast<char*>(&val), sizeof(val));
            stat.p9999Ns = val;

            uint8_t flags = 0;
            ifs.read(reinterpret_cast<char*>(&flags), sizeof(flags));
            stat.hasP99 = (flags & 0x01) != 0;
            stat.hasP999 = (flags & 0x02) != 0;
            stat.hasP9999 = (flags & 0x04) != 0;
#endif

            uint8_t level = 0;
            ifs.read(reinterpret_cast<char*>(&level), sizeof(level));
            stat.level = level;

            uint16_t modLen = 0;
            ifs.read(reinterpret_cast<char*>(&modLen), sizeof(modLen));
            stat.moduleName.resize(modLen);
            if (modLen > 0) {
                ifs.read(&stat.moduleName[0], modLen);
            }

            uint16_t ptLen = 0;
            ifs.read(reinterpret_cast<char*>(&ptLen), sizeof(ptLen));
            stat.pointName.resize(ptLen);
            if (ptLen > 0) {
                ifs.read(&stat.pointName[0], ptLen);
            }

            uint16_t pgmLen = 0;
            ifs.read(reinterpret_cast<char*>(&pgmLen), sizeof(pgmLen));
            stat.programName.resize(pgmLen);
            if (pgmLen > 0) {
                ifs.read(&stat.programName[0], pgmLen);
            }
        }
    }

    return ifs.good() || ifs.eof();
}

uint32_t DumpRecorder::Capacity() const {
    return capacity_;
}

void DumpRecorder::SetCapacity(uint32_t newCapacity) {
    uint32_t numToCopy = std::min(count_, newCapacity);
    std::vector<DumpRecord> newRecords(newCapacity);

    for (uint32_t i = 0; i < numToCopy; ++i) {
        size_t logicalIdx = numToCopy - 1 - i;
        newRecords[i] = records_[PhysicalIndex(logicalIdx)];
    }

    records_ = std::move(newRecords);
    capacity_ = newCapacity;
    count_ = numToCopy;
    head_ = numToCopy % newCapacity;
}

size_t DumpRecorder::PhysicalIndex(size_t logicalIndex) const {
    return (head_ + capacity_ - 1 - logicalIndex) % capacity_;
}

}
