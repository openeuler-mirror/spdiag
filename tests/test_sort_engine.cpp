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
#include "cli/sort_engine.h"
#include "cli/sort_spec.h"

#include <vector>

using namespace SpDiag::Cli;

namespace {
struct Item {
    int a;
    int b;
    int order;  // insertion-order marker for stability checks
};

// NAME -> compare .a, LVL -> compare .b. The enum values are just map keys;
// any valid SortKey::Key would do.
SortAccessorMap<Item> MakeDefaultAccessors() {
    SortAccessorMap<Item> accessors;
    accessors[SortKey::NAME] =
        [](const Item& x, const Item& y) { return ThreeWayCompare(x.a, y.a); };
    accessors[SortKey::LVL] =
        [](const Item& x, const Item& y) { return ThreeWayCompare(x.b, y.b); };
    return accessors;
}
}  // namespace

TEST(ThreeWayCompareTest, IntSignatures) {
    EXPECT_EQ(ThreeWayCompare<int>(1, 2), -1);
    EXPECT_EQ(ThreeWayCompare<int>(2, 2), 0);
    EXPECT_EQ(ThreeWayCompare<int>(3, 2), 1);
}

TEST(ApplyMultiSortTest, SingleKeyAscending) {
    auto accessors = MakeDefaultAccessors();
    std::vector<Item> items = {{3, 0, 0}, {1, 0, 0}, {2, 0, 0}};
    std::vector<SortKey> keys = {{SortKey::NAME, false}};
    ApplyMultiSort(items, keys, accessors);
    ASSERT_EQ(items.size(), 3u);
    EXPECT_EQ(items[0].a, 1);
    EXPECT_EQ(items[1].a, 2);
    EXPECT_EQ(items[2].a, 3);
}

TEST(ApplyMultiSortTest, SingleKeyDescending) {
    auto accessors = MakeDefaultAccessors();
    std::vector<Item> items = {{1, 0, 0}, {3, 0, 0}, {2, 0, 0}};
    std::vector<SortKey> keys = {{SortKey::NAME, true}};
    ApplyMultiSort(items, keys, accessors);
    ASSERT_EQ(items.size(), 3u);
    EXPECT_EQ(items[0].a, 3);
    EXPECT_EQ(items[1].a, 2);
    EXPECT_EQ(items[2].a, 1);
}

TEST(ApplyMultiSortTest, MultiKeyLexicographicTieBreak) {
    auto accessors = MakeDefaultAccessors();
    std::vector<Item> items = {{1, 2, 0}, {2, 5, 0}, {1, 1, 0}, {2, 1, 0}};
    std::vector<SortKey> keys = {{SortKey::NAME, false}, {SortKey::LVL, false}};
    ApplyMultiSort(items, keys, accessors);
    ASSERT_EQ(items.size(), 4u);
    // primary .a ascending; ties broken by secondary .b ascending.
    EXPECT_EQ(items[0].a, 1); EXPECT_EQ(items[0].b, 1);
    EXPECT_EQ(items[1].a, 1); EXPECT_EQ(items[1].b, 2);
    EXPECT_EQ(items[2].a, 2); EXPECT_EQ(items[2].b, 1);
    EXPECT_EQ(items[3].a, 2); EXPECT_EQ(items[3].b, 5);
}

TEST(ApplyMultiSortTest, StableSortPreservesInsertionOrderOnTies) {
    auto accessors = MakeDefaultAccessors();
    // Every element ties on .a (the only key used); stable sort must keep
    // insertion order, which is recorded in .order.
    std::vector<Item> items = {
        {5, 0, 0}, {5, 0, 1}, {5, 0, 2}, {5, 0, 3}
    };
    std::vector<SortKey> keys = {{SortKey::NAME, false}};
    ApplyMultiSort(items, keys, accessors);
    ASSERT_EQ(items.size(), 4u);
    for (size_t i = 0; i < items.size(); ++i) {
        EXPECT_EQ(items[i].order, static_cast<int>(i));
    }
}

TEST(ApplyMultiSortTest, EmptyKeysVectorLeavesOrderUntouched) {
    auto accessors = MakeDefaultAccessors();
    std::vector<Item> items = {{3, 0, 0}, {1, 0, 1}, {2, 0, 2}};
    std::vector<SortKey> keys;
    ApplyMultiSort(items, keys, accessors);
    ASSERT_EQ(items.size(), 3u);
    EXPECT_EQ(items[0].a, 3);
    EXPECT_EQ(items[1].a, 1);
    EXPECT_EQ(items[2].a, 2);
}

TEST(ApplyMultiSortTest, MissingAccessorKeyIsSkipped) {
    auto accessors = MakeDefaultAccessors();
    // PEAK has no entry in accessors -> skipped; NAME still sorts by .a.
    std::vector<Item> items = {{3, 0, 0}, {1, 0, 0}, {2, 0, 0}};
    std::vector<SortKey> keys = {{SortKey::PEAK, false}, {SortKey::NAME, false}};
    ApplyMultiSort(items, keys, accessors);
    ASSERT_EQ(items.size(), 3u);
    EXPECT_EQ(items[0].a, 1);
    EXPECT_EQ(items[1].a, 2);
    EXPECT_EQ(items[2].a, 3);
}
