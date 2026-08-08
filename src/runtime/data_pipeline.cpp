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

#include "data_pipeline.h"

#include "sdk/platform.h"

namespace SpDiag {

uint32_t DataPipeline::CreateChannel(const std::string& pluginName) {
    std::lock_guard<std::mutex> guard(mutex_);

    const uint32_t channelId = nextChannelId_++;
    ChannelState state;
    state.pluginName = pluginName;
    channels_.emplace(channelId, std::move(state));
    return channelId;
}

bool DataPipeline::DestroyChannel(uint32_t channelId) {
    std::lock_guard<std::mutex> guard(mutex_);
    return channels_.erase(channelId) > 0;
}

bool DataPipeline::SubmitData(uint32_t channelId, const ProcessorData& data) {
    std::lock_guard<std::mutex> guard(mutex_);

    auto it = channels_.find(channelId);
    if (it == channels_.end()) {
        return false;
    }

    it->second.queue.push_back(data);
    outputQueue_.push_back(ProcessData(data));
    return true;
}

DataPipeline::ChannelDataSink::ChannelDataSink(DataPipeline& pipeline, uint32_t channelId)
    : pipeline_(pipeline), channelId_(channelId) {}

bool DataPipeline::ChannelDataSink::Submit(const std::string& key, const std::string& value) {
    ProcessorData data;
    data.timestampNs = Platform::GetTimestampNs();
    data.key = key;
    data.value = value;
    return pipeline_.SubmitData(channelId_, data);
}

bool DataPipeline::ChannelDataSink::SubmitBatch(const std::vector<std::pair<std::string, std::string>>& items) {
    for (const auto& [k, v] : items) {
        if (!Submit(k, v)) {
            return false;
        }
    }
    return true;
}

std::unique_ptr<DataSink> DataPipeline::CreateSink(uint32_t channelId) {
    return std::make_unique<ChannelDataSink>(*this, channelId);
}

std::vector<ProcessorOutput> DataPipeline::GetOutput() {
    std::lock_guard<std::mutex> guard(mutex_);

    std::vector<ProcessorOutput> out;
    out.swap(outputQueue_);
    return out;
}

SymbolResolver& DataPipeline::GetSymbolResolver() {
    return symbolResolver_;
}

ProcessorOutput DataPipeline::ProcessData(const ProcessorData& data) {
    ProcessorOutput out;

    uint64_t normalizedTimestamp = data.timestampNs;
    if (normalizedTimestamp == 0) {
        normalizedTimestamp = Platform::GetTimestampNs();
    }
    out.normalizedTimestampNs = normalizedTimestamp;

    SymbolInfo symbol;
    if (data.pid > 0 && data.address != 0 && symbolResolver_.Resolve(data.pid, data.address, symbol)) {
        out.symbolName = symbol.symbolName;
        out.filePath = symbol.filePath;
    } else {
        out.symbolName = "<na>";
        out.filePath = "";
    }

    const uint64_t sampleCount = data.sampleCount == 0 ? 1 : data.sampleCount;
    out.key = data.key;
    if (sampleCount > 1) {
        out.value = data.value + " (x" + std::to_string(sampleCount) + ")";
    } else {
        out.value = data.value;
    }

    return out;
}

}
