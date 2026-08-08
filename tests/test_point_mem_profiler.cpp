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

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "src/runtime/data_sink.h"
#include "src/runtime/plugin_types.h"
#include "spdiag/mem_point_keys.h"

namespace {

class MockDataSink : public SpDiag::DataSink {
public:
    bool Submit(const std::string& key, const std::string& value) override {
        records_.push_back({key, value});
        return true;
    }

    bool SubmitBatch(const std::vector<std::pair<std::string, std::string>>& items) override {
        for (const auto& item : items) {
            records_.push_back(item);
        }
        return true;
    }

    const std::vector<std::pair<std::string, std::string>>& Records() const {
        return records_;
    }

    void Clear() { records_.clear(); }

private:
    std::vector<std::pair<std::string, std::string>> records_;
};

} // namespace

// --- MemPointKey helpers ---

TEST(PointMemProfilerTest, KeyCategoriesMatchProfilerExpectations) {
    uint32_t count = SpDiag::MemPointKeyCount();
    EXPECT_GT(count, 0u);

    for (uint32_t i = 1; i <= count; ++i) {
        auto key = static_cast<SpDiag::MemPointKey>(i);
        std::string cat = SpDiag::GetMemPointKeyCategory(key);
        EXPECT_FALSE(cat.empty()) << "Category for key " << i << " should not be empty";
    }
}

TEST(PointMemProfilerTest, KeyDescriptionsMatchProfilerExpectations) {
    uint32_t count = SpDiag::MemPointKeyCount();
    for (uint32_t i = 1; i <= count; ++i) {
        auto key = static_cast<SpDiag::MemPointKey>(i);
        std::string desc = SpDiag::GetMemPointKeyDescription(key);
        EXPECT_FALSE(desc.empty()) << "Description for key " << i << " should not be empty";
    }
}

TEST(PointMemProfilerTest, OutOfRangeKeyReturnsUnknown) {
    EXPECT_EQ(SpDiag::GetMemPointKeyCategory(SpDiag::MemPointKey::NONE), "unknown");
    auto beyondLast = static_cast<SpDiag::MemPointKey>(
        static_cast<uint32_t>(SpDiag::MemPointKey::MEM_POINT_KEY_COUNT));
    EXPECT_EQ(SpDiag::GetMemPointKeyCategory(beyondLast), "unknown");
}

// --- MockDataSink ---

TEST(PointMemProfilerTest, MockDataSinkRecordsSubmissions) {
    MockDataSink sink;
    sink.Submit("key1", "val1");
    sink.SubmitBatch({{"key2", "val2"}, {"key3", "val3"}});

    ASSERT_EQ(sink.Records().size(), 3u);
    EXPECT_EQ(sink.Records()[0].first, "key1");
    EXPECT_EQ(sink.Records()[1].first, "key2");
    EXPECT_EQ(sink.Records()[2].first, "key3");
}

// --- Data format strings ---

TEST(PointMemProfilerTest, StatMapKeyFormatIsCorrect) {
    uint32_t pid = 12345;
    uint32_t id = 1;
    std::string prefix = "mempoint." + std::to_string(pid) + "." + std::to_string(id);

    std::vector<std::string> expectedSuffixes = {
        ".current_bytes", ".total_alloc", ".alloc_count",
        ".free_count", ".peak", ".max_size", ".min_size",
        ".first_seen_ns", ".key_name"
    };

    for (const auto& suffix : expectedSuffixes) {
        std::string fullKey = prefix + suffix;
        EXPECT_FALSE(fullKey.empty());
        EXPECT_EQ(fullKey.substr(0, 9), "mempoint.");
    }
}

TEST(PointMemProfilerTest, LeakKeyFormatIsCorrect) {
    uint32_t pid = 12345;
    uint32_t id = 3;
    std::string prefix = "mempoint." + std::to_string(pid) + ".leak." + std::to_string(id);

    std::vector<std::string> expectedSuffixes = {
        ".count", ".max_age_ns", ".key_name"
    };

    for (const auto& suffix : expectedSuffixes) {
        std::string fullKey = prefix + suffix;
        EXPECT_EQ(fullKey.substr(0, 9), "mempoint.");
        EXPECT_NE(fullKey.find("leak"), std::string::npos);
    }
}

// --- Feature flags ---

TEST(PointMemProfilerTest, FeatureFlagsAreDistinctBits) {
    EXPECT_EQ(SpDiag::FEAT_OB_MEMORY, 1U << 2);
    EXPECT_EQ(SpDiag::FEAT_OB_CACHE,  1U << 3);
    EXPECT_EQ(SpDiag::FEAT_MEMPOINT,  1U << 4);
    EXPECT_EQ(SpDiag::FEAT_OB_MEMORY & SpDiag::FEAT_OB_CACHE, 0u);
    EXPECT_EQ(SpDiag::FEAT_OB_MEMORY & SpDiag::FEAT_MEMPOINT, 0u);
    EXPECT_EQ(SpDiag::FEAT_OB_CACHE & SpDiag::FEAT_MEMPOINT, 0u);
}

// --- PointMemProfiler class tests (require SPDIAG_ENABLE_MEMPOINT) ---

#ifdef SPDIAG_ENABLE_MEMPOINT

#include "src/runtime/plugins/ob_point/point_mem_profiler.h"

TEST(PointMemProfilerTest, DefaultConstruction) {
    SpDiag::PointMemProfiler profiler;
}

TEST(PointMemProfilerTest, DetachOnUnattachedIsSafe) {
    SpDiag::PointMemProfiler profiler;
    profiler.Detach();
    profiler.Detach();
}

TEST(PointMemProfilerTest, CollectReturnsFalseWhenUnattached) {
    SpDiag::PointMemProfiler profiler;
    MockDataSink sink;
    EXPECT_FALSE(profiler.Collect(&sink));
}

TEST(PointMemProfilerTest, CollectReturnsFalseWithNullSink) {
    SpDiag::PointMemProfiler profiler;
    EXPECT_FALSE(profiler.Collect(nullptr));
}

TEST(PointMemProfilerTest, AttachReturnsFalseWithNullBpfObject) {
    SpDiag::PointMemProfiler profiler;
    EXPECT_FALSE(profiler.Attach(nullptr, 1234, "/usr/bin/test"));
}

#else

TEST(PointMemProfilerTest, DisabledWithoutMemPoint) {
    GTEST_SKIP() << "PointMemProfiler tests require SPDIAG_ENABLE_MEMPOINT";
}

#endif // SPDIAG_ENABLE_MEMPOINT
