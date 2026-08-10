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

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

#include "spdiag/perf_config.h"
#include "spdiag/perf_level.h"
#include "spdiag/perf_manager.h"

// Shared test helpers for SpDiag unit/integration tests. Centralises the
// PerfManager Init+skip boilerplate and the temporary-directory RAII helper
// that were previously duplicated across the test suite.

namespace spdiag_test {

// Builds a PerfConfig from the given shared-memory name and tunables, calls
// PerfManager::Instance().Init, and returns IsInitialized(). The defaults
// (levelGate = DEBUG so every level is enabled, RunMode::DEBUG) match what
// most existing tests pass. Callers that need SHM should follow this with
// SPDIAG_SKIP_IF_NO_SHM() so the test is skipped on platforms without
// shared-memory support.
inline bool InitPerfManagerForTest(const std::string& shmName,
                                   uint8_t levelGate = static_cast<uint8_t>(SpDiag::PerfLevel::DEBUG),
                                   SpDiag::RunMode mode = SpDiag::RunMode::DEBUG,
                                   uint32_t dumpIntervalMs = 1000) {
    SpDiag::PerfConfig config;
    config.shmName = shmName;
    config.mode = mode;
    config.levelGate = levelGate;
    config.dumpIntervalMs = dumpIntervalMs;
    SpDiag::PerfManager::Instance().Init(config);
    return SpDiag::PerfManager::Instance().IsInitialized();
}

// Skips the current test when PerfManager did not initialise (no SHM). Keeps
// the exact skip message the tests previously used inline.
#define SPDIAG_SKIP_IF_NO_SHM()                                                   \
    do {                                                                          \
        if (!SpDiag::PerfManager::Instance().IsInitialized()) {                   \
            GTEST_SKIP() << "PerfManager not initialized (no SHM)";               \
        }                                                                         \
    } while (0)

// RAII wrapper around a unique temporary directory. The constructor creates
// the directory under the system temp path; the destructor removes it.
class TempDir {
public:
    TempDir() {
        path_ = std::filesystem::temp_directory_path() /
            ("spdiag_csv_writer_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path_);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    const std::filesystem::path& Path() const { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace spdiag_test
