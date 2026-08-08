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
#include <memory>
#include "spdiag/shm_layout.h"
#include "manager/aggregator.h"
#include "manager/dump_recorder.h"
#include "cli_config.h"
#include "sort_spec.h"
#include "runtime/data_pipeline.h"
#include "mempoint_display.h"
#include "cache_display.h"
#include "csv_writer.h"

namespace SpDiag {

struct DisplayOptions {
    bool        detail      = false;
    int32_t     coreStart   = -1;
    int32_t     coreEnd     = -1;
    uint8_t     levelFilter = 0;
    LevelOp     levelOp     = LevelOp::LE;
    bool        hasLevelRange   = false;
    uint8_t     levelRangeStart = 0;
    uint8_t     levelRangeEnd   = 0;
    std::vector<Cli::SortKey> sortKeys;
    std::vector<std::string> filterGlobs;
};

struct ShmInfo {
    uint32_t numCores = 0;
    uint32_t maxSlots = 0;
    uint32_t maxPrograms = 0;
#ifdef SPDIAG_ENABLE_PERFLOG
    uint8_t  perflogEnabled = 0;
    uint64_t perflogBootCounter = 0;
    uint64_t perflogBootSystemNs = 0;
    uint64_t perflogTimerFreq = 0;
#endif
};

class DisplayEngine {
public:
    ~DisplayEngine() { csvWriter_.EndSession(); }

    void ShowSummary(const std::vector<AggregatedStat>& stats, const DisplayOptions& opts);
    void ShowDetail(const ShmInfo& info, const ShardBuffer& shards,
                    const ProgramEntry* programs, uint32_t numPrograms,
                    const char (*keyPointNames)[nameLen],
                    const char (*keyModuleNames)[nameLen],
                    const DisplayOptions& opts);
    void ShowRawTable(const ShmInfo& info, const ShardBuffer& shards,
                      const ProgramEntry* programs, uint32_t numPrograms,
                      const char (*keyPointNames)[nameLen],
                      const char (*keyModuleNames)[nameLen],
                      int32_t coreStart = -1, int32_t coreEnd = -1);
    void ShowHistory(const std::vector<DumpRecord>& records, int32_t maxCount);
    void ShowHistoryDetail(const DumpRecord& record);
    void ShowObMemory(const std::vector<ProcessorOutput>& outputs, bool humanReadable = true, bool noFilter = false, bool detail = false, const std::string& groupBy = "tid+frame", int top = 20, const std::string& showFields = "",
                      const std::vector<Cli::SortKey>& sortKeys = {}, const std::vector<std::string>& filterGlobs = {});
    void ShowObCache(const std::vector<ProcessorOutput>& outputs, bool cumulative = true);
    void ShowMemPoint(const std::vector<ProcessorOutput>& outputs,
                      bool humanReadable = true, int top = 20,
                      const std::vector<Cli::SortKey>& sortKeys = {},
                      const std::vector<std::string>& filterGlobs = {},
                      const std::string& showFields = "");
    void ShowEmptySummary();
    void ShowEmptyDetail();
#ifdef SPDIAG_ENABLE_PERFLOG
    void ShowPerfLog(const ShmInfo& info, const ShardBuffer& shards,
                    const ProgramEntry* programs, uint32_t numPrograms,
                    const char (*keyPointNames)[nameLen],
                    const char (*keyModuleNames)[nameLen]);
#endif
    void ClearScreen();
    void MoveCursorHome();

    Cli::CsvWriter& GetCsvWriter() { return csvWriter_; }

private:
    std::string FormatNs(uint64_t value, int width) const;
    std::string FormatPercentile(uint64_t value, bool valid, int width) const;
    void PrintSummaryHeader();
    void PrintDetailHeader();
    double ComputeSession();

    Cli::CacheDisplayState cacheDisplayState_;
    uint64_t memSessionStartNs_ = 0;
    Cli::CsvWriter csvWriter_;
};

}
