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
#include "aggregator.h"

namespace SpDiag {

class PersistentAggr {
public:
    explicit PersistentAggr(const std::string& dataDir);

    bool Exists() const;

    std::vector<AggregatedStat> Load();

    std::vector<AggregatedStat> Update(const std::vector<AggregatedStat>& currentSnapshot);

    const std::string& DataDir() const;

private:
    std::string dataDir_;
    std::string aggrFilePath_;
    std::string snapshotFilePath_;

    std::vector<AggregatedStat> LoadSnapshot();
    bool SaveSnapshot(const std::vector<AggregatedStat>& snapshot);
    bool SaveAggr(const std::vector<AggregatedStat>& aggr);
    std::vector<AggregatedStat> LoadAggr();

    bool SaveDataFile(const std::string& path, uint32_t magic, const std::vector<AggregatedStat>& data);
    std::vector<AggregatedStat> LoadDataFile(const std::string& path, uint32_t magic);

    std::vector<AggregatedStat> ComputeDelta(
        const std::vector<AggregatedStat>& current,
        const std::vector<AggregatedStat>& previous);
};

}
