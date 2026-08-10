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

#include "persistent_aggr.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstring>

namespace SpDiag {

static constexpr uint32_t AGGR_MAGIC = 0x41474752;
static constexpr uint32_t SNAPSHOT_MAGIC = 0x534E5053;
static constexpr uint32_t FILE_VERSION = 1;

PersistentAggr::PersistentAggr(const std::string& dataDir)
    : dataDir_(dataDir)
    , aggrFilePath_(dataDir + "/aggr.bin")
    , snapshotFilePath_(dataDir + "/snapshot.bin") {
}

bool PersistentAggr::Exists() const {
    std::ifstream f(aggrFilePath_, std::ios::binary);
    return f.is_open();
}

std::vector<AggregatedStat> PersistentAggr::Load() {
    if (!Exists()) {
        return {};
    }
    return LoadAggr();
}

static bool WriteStat(std::ofstream& out, const AggregatedStat& stat) {
    out.write(reinterpret_cast<const char*>(&stat.globalSlotIndex), sizeof(stat.globalSlotIndex));
    out.write(reinterpret_cast<const char*>(&stat.totalGoodCount), sizeof(stat.totalGoodCount));
    out.write(reinterpret_cast<const char*>(&stat.totalBadCount), sizeof(stat.totalBadCount));
    out.write(reinterpret_cast<const char*>(&stat.totalNotCount), sizeof(stat.totalNotCount));
    out.write(reinterpret_cast<const char*>(&stat.totalTickCount), sizeof(stat.totalTickCount));
    out.write(reinterpret_cast<const char*>(&stat.totalTimeNs), sizeof(stat.totalTimeNs));
    out.write(reinterpret_cast<const char*>(&stat.globalMinTimeNs), sizeof(stat.globalMinTimeNs));
    out.write(reinterpret_cast<const char*>(&stat.globalMaxTimeNs), sizeof(stat.globalMaxTimeNs));
    out.write(reinterpret_cast<const char*>(&stat.avgTimeNs), sizeof(stat.avgTimeNs));

#ifdef SPDIAG_ENABLE_PERCENTILE
    uint32_t topNCount = static_cast<uint32_t>(stat.globalTopN.size());
    out.write(reinterpret_cast<const char*>(&topNCount), sizeof(topNCount));
    for (uint32_t i = 0; i < topNCount; ++i) {
        out.write(reinterpret_cast<const char*>(&stat.globalTopN[i]), sizeof(uint64_t));
    }

    out.write(reinterpret_cast<const char*>(&stat.p99Ns), sizeof(stat.p99Ns));
    out.write(reinterpret_cast<const char*>(&stat.p999Ns), sizeof(stat.p999Ns));
    out.write(reinterpret_cast<const char*>(&stat.p9999Ns), sizeof(stat.p9999Ns));

    uint8_t flags = 0;
    if (stat.hasP99) flags |= 0x01;
    if (stat.hasP999) flags |= 0x02;
    if (stat.hasP9999) flags |= 0x04;
    out.write(reinterpret_cast<const char*>(&flags), sizeof(flags));
#endif

    out.write(reinterpret_cast<const char*>(&stat.level), sizeof(stat.level));

    uint16_t modLen = static_cast<uint16_t>(stat.moduleName.size());
    out.write(reinterpret_cast<const char*>(&modLen), sizeof(modLen));
    if (modLen > 0) {
        out.write(stat.moduleName.data(), modLen);
    }

    uint16_t ptLen = static_cast<uint16_t>(stat.pointName.size());
    out.write(reinterpret_cast<const char*>(&ptLen), sizeof(ptLen));
    if (ptLen > 0) {
        out.write(stat.pointName.data(), ptLen);
    }

    uint16_t pgmLen = static_cast<uint16_t>(stat.programName.size());
    out.write(reinterpret_cast<const char*>(&pgmLen), sizeof(pgmLen));
    if (pgmLen > 0) {
        out.write(stat.programName.data(), pgmLen);
    }

    return out.good();
}

static bool ReadStat(std::ifstream& in, AggregatedStat& stat) {
    in.read(reinterpret_cast<char*>(&stat.globalSlotIndex), sizeof(stat.globalSlotIndex));
    in.read(reinterpret_cast<char*>(&stat.totalGoodCount), sizeof(stat.totalGoodCount));
    in.read(reinterpret_cast<char*>(&stat.totalBadCount), sizeof(stat.totalBadCount));
    in.read(reinterpret_cast<char*>(&stat.totalNotCount), sizeof(stat.totalNotCount));
    in.read(reinterpret_cast<char*>(&stat.totalTickCount), sizeof(stat.totalTickCount));
    in.read(reinterpret_cast<char*>(&stat.totalTimeNs), sizeof(stat.totalTimeNs));
    in.read(reinterpret_cast<char*>(&stat.globalMinTimeNs), sizeof(stat.globalMinTimeNs));
    in.read(reinterpret_cast<char*>(&stat.globalMaxTimeNs), sizeof(stat.globalMaxTimeNs));
    in.read(reinterpret_cast<char*>(&stat.avgTimeNs), sizeof(stat.avgTimeNs));

#ifdef SPDIAG_ENABLE_PERCENTILE
    uint32_t topNCount = 0;
    in.read(reinterpret_cast<char*>(&topNCount), sizeof(topNCount));
    stat.globalTopN.resize(topNCount);
    for (uint32_t i = 0; i < topNCount; ++i) {
        in.read(reinterpret_cast<char*>(&stat.globalTopN[i]), sizeof(uint64_t));
    }

    in.read(reinterpret_cast<char*>(&stat.p99Ns), sizeof(stat.p99Ns));
    in.read(reinterpret_cast<char*>(&stat.p999Ns), sizeof(stat.p999Ns));
    in.read(reinterpret_cast<char*>(&stat.p9999Ns), sizeof(stat.p9999Ns));

    uint8_t flags = 0;
    in.read(reinterpret_cast<char*>(&flags), sizeof(flags));
    stat.hasP99 = (flags & 0x01) != 0;
    stat.hasP999 = (flags & 0x02) != 0;
    stat.hasP9999 = (flags & 0x04) != 0;
#endif

    in.read(reinterpret_cast<char*>(&stat.level), sizeof(stat.level));

    uint16_t modLen = 0;
    in.read(reinterpret_cast<char*>(&modLen), sizeof(modLen));
    stat.moduleName.resize(modLen);
    if (modLen > 0) {
        in.read(&stat.moduleName[0], modLen);
    }

    uint16_t ptLen = 0;
    in.read(reinterpret_cast<char*>(&ptLen), sizeof(ptLen));
    stat.pointName.resize(ptLen);
    if (ptLen > 0) {
        in.read(&stat.pointName[0], ptLen);
    }

    uint16_t pgmLen = 0;
    in.read(reinterpret_cast<char*>(&pgmLen), sizeof(pgmLen));
    stat.programName.resize(pgmLen);
    if (pgmLen > 0) {
        in.read(&stat.programName[0], pgmLen);
    }

    return in.good() || in.eof();
}

bool PersistentAggr::SaveDataFile(const std::string& path, uint32_t magic, const std::vector<AggregatedStat>& data) {
    std::filesystem::create_directories(dataDir_);
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return false;

    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&FILE_VERSION), sizeof(FILE_VERSION));

    uint32_t count = static_cast<uint32_t>(data.size());
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (const auto& stat : data) {
        if (!WriteStat(out, stat)) return false;
    }

    return out.good();
}

std::vector<AggregatedStat> PersistentAggr::LoadDataFile(const std::string& path, uint32_t magic) {
    std::vector<AggregatedStat> result;
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return result;

    uint32_t fileMagic = 0;
    in.read(reinterpret_cast<char*>(&fileMagic), sizeof(fileMagic));
    if (fileMagic != magic) return result;

    uint32_t version = 0;
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version != FILE_VERSION) return result;

    uint32_t count = 0;
    in.read(reinterpret_cast<char*>(&count), sizeof(count));

    result.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        AggregatedStat stat;
        if (!ReadStat(in, stat)) break;
        result.push_back(std::move(stat));
    }

    return result;
}

bool PersistentAggr::SaveAggr(const std::vector<AggregatedStat>& aggr) {
    return SaveDataFile(aggrFilePath_, AGGR_MAGIC, aggr);
}

std::vector<AggregatedStat> PersistentAggr::LoadAggr() {
    return LoadDataFile(aggrFilePath_, AGGR_MAGIC);
}

bool PersistentAggr::SaveSnapshot(const std::vector<AggregatedStat>& snapshot) {
    return SaveDataFile(snapshotFilePath_, SNAPSHOT_MAGIC, snapshot);
}

std::vector<AggregatedStat> PersistentAggr::LoadSnapshot() {
    return LoadDataFile(snapshotFilePath_, SNAPSHOT_MAGIC);
}

std::vector<AggregatedStat> PersistentAggr::Update(const std::vector<AggregatedStat>& currentSnapshot) {
    if (!Exists()) {
        std::filesystem::create_directories(dataDir_);
        SaveAggr(currentSnapshot);
        SaveSnapshot(currentSnapshot);
        return currentSnapshot;
    }

    auto previousSnapshot = LoadSnapshot();
    auto delta = ComputeDelta(currentSnapshot, previousSnapshot);
    auto accumulated = LoadAggr();

    for (const auto& d : delta) {
        AggregatedStat* match = nullptr;
        for (auto& existing : accumulated) {
            if (existing.globalSlotIndex == d.globalSlotIndex) {
                match = &existing;
                break;
            }
            if (!d.moduleName.empty() && !d.pointName.empty()
                && existing.moduleName == d.moduleName
                && existing.pointName == d.pointName) {
                match = &existing;
                break;
            }
        }

        if (match) {
            MergeStat(*match, d);

            if (match->globalSlotIndex != d.globalSlotIndex) {
                match->globalSlotIndex = d.globalSlotIndex;
            }
            if (d.moduleName != match->moduleName && !d.moduleName.empty()) {
                match->moduleName = d.moduleName;
            }
            if (d.pointName != match->pointName && !d.pointName.empty()) {
                match->pointName = d.pointName;
            }
            if (d.programName != match->programName && !d.programName.empty()) {
                match->programName = d.programName;
            }
            match->level = d.level;
        } else {
            accumulated.push_back(d);
        }
    }

    for (auto& stat : accumulated) {
        RecalcAvg(stat);
    }

    if (!SaveAggr(accumulated)) return delta;
    if (!SaveSnapshot(currentSnapshot)) return delta;
    return delta;
}

std::vector<AggregatedStat> PersistentAggr::ComputeDelta(
    const std::vector<AggregatedStat>& current,
    const std::vector<AggregatedStat>& previous) {

    std::vector<AggregatedStat> delta;

    for (const auto& cur : current) {
        AggregatedStat d;
        d.globalSlotIndex = cur.globalSlotIndex;

        const AggregatedStat* prevMatch = nullptr;
        for (const auto& prev : previous) {
            if (prev.globalSlotIndex == cur.globalSlotIndex) {
                prevMatch = &prev;
                break;
            }
            if (!cur.moduleName.empty() && !cur.pointName.empty()
                && prev.moduleName == cur.moduleName
                && prev.pointName == cur.pointName) {
                prevMatch = &prev;
                break;
            }
        }

        if (prevMatch) {
            if (cur.totalTickCount >= prevMatch->totalTickCount) {
                d.totalGoodCount = cur.totalGoodCount - prevMatch->totalGoodCount;
                d.totalBadCount = cur.totalBadCount - prevMatch->totalBadCount;
                d.totalNotCount = cur.totalNotCount - prevMatch->totalNotCount;
                d.totalTickCount = cur.totalTickCount - prevMatch->totalTickCount;
                d.totalTimeNs = cur.totalTimeNs - prevMatch->totalTimeNs;
            } else {
                d.totalGoodCount = cur.totalGoodCount;
                d.totalBadCount = cur.totalBadCount;
                d.totalNotCount = cur.totalNotCount;
                d.totalTickCount = cur.totalTickCount;
                d.totalTimeNs = cur.totalTimeNs;
            }
            d.globalMinTimeNs = cur.globalMinTimeNs;
            d.globalMaxTimeNs = cur.globalMaxTimeNs;
#ifdef SPDIAG_ENABLE_PERCENTILE
            d.globalTopN = cur.globalTopN;
            d.p99Ns = cur.p99Ns;
            d.p999Ns = cur.p999Ns;
            d.p9999Ns = cur.p9999Ns;
            d.hasP99 = cur.hasP99;
            d.hasP999 = cur.hasP999;
            d.hasP9999 = cur.hasP9999;
#endif
            d.moduleName = cur.moduleName;
            d.pointName = cur.pointName;
            d.programName = cur.programName;
            d.level = cur.level;
        } else {
            d = cur;
        }

        delta.push_back(std::move(d));
    }

    return delta;
}

const std::string& PersistentAggr::DataDir() const {
    return dataDir_;
}

}
