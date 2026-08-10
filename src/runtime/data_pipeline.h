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
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "data_sink.h"
#include "symbol_resolver.h"

namespace SpDiag {

struct ProcessorData {
    uint64_t timestampNs = 0;
    uint64_t sampleCount = 1;
    int32_t pid = -1;
    uint64_t address = 0;
    std::string key;
    std::string value;
};

struct ProcessorOutput {
    uint64_t normalizedTimestampNs = 0;
    std::string symbolName;
    std::string filePath;
    std::string key;
    std::string value;
};

class DataPipeline {
public:
    DataPipeline() = default;

    uint32_t CreateChannel(const std::string& pluginName);
    bool DestroyChannel(uint32_t channelId);
    bool SubmitData(uint32_t channelId, const ProcessorData& data);

    std::unique_ptr<DataSink> CreateSink(uint32_t channelId);

    std::vector<ProcessorOutput> GetOutput();
    SymbolResolver& GetSymbolResolver();

private:
    struct ChannelState {
        std::string pluginName;
        std::vector<ProcessorData> queue;
    };

    class ChannelDataSink : public DataSink {
    public:
        ChannelDataSink(DataPipeline& pipeline, uint32_t channelId);
        bool Submit(const std::string& key, const std::string& value) override;
        bool SubmitBatch(const std::vector<std::pair<std::string, std::string>>& items) override;
    private:
        DataPipeline& pipeline_;
        uint32_t channelId_;
    };

    ProcessorOutput ProcessData(const ProcessorData& data);

    std::mutex mutex_;
    uint32_t nextChannelId_ = 1;
    std::unordered_map<uint32_t, ChannelState> channels_;
    std::vector<ProcessorOutput> outputQueue_;
    SymbolResolver symbolResolver_;
};

}
