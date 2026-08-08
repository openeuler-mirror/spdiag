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

#ifdef SPDIAG_ENABLE_PERCENTILE

#include <gtest/gtest.h>
#include "src/manager/percentile_calculator.h"

using SpDiag::CalculatePercentiles;

TEST(PercentileTest, NormalCalculation) {
    std::vector<uint64_t> topN = {500, 400, 300, 200, 100};
    uint64_t tickCount = 100;
    uint64_t p99Ns, p999Ns, p9999Ns;
    bool hasP99, hasP999, hasP9999;

    CalculatePercentiles(topN, tickCount, p99Ns, hasP99, p999Ns, hasP999, p9999Ns, hasP9999);

    EXPECT_TRUE(hasP99);
    EXPECT_EQ(400u, p99Ns);
    EXPECT_TRUE(hasP999);
    EXPECT_EQ(500u, p999Ns);
    EXPECT_TRUE(hasP9999);
    EXPECT_EQ(500u, p9999Ns);
}

TEST(PercentileTest, TopNTooSmall) {
    std::vector<uint64_t> topN = {500, 400, 300};
    uint64_t tickCount = 10000;
    uint64_t p99Ns, p999Ns, p9999Ns;
    bool hasP99, hasP999, hasP9999;

    CalculatePercentiles(topN, tickCount, p99Ns, hasP99, p999Ns, hasP999, p9999Ns, hasP9999);

    EXPECT_FALSE(hasP99);
    EXPECT_FALSE(hasP999);
    EXPECT_TRUE(hasP9999);
    EXPECT_EQ(400u, p9999Ns);
}

TEST(PercentileTest, EmptyTopN) {
    std::vector<uint64_t> topN;
    uint64_t tickCount = 100;
    uint64_t p99Ns, p999Ns, p9999Ns;
    bool hasP99, hasP999, hasP9999;

    CalculatePercentiles(topN, tickCount, p99Ns, hasP99, p999Ns, hasP999, p9999Ns, hasP9999);

    EXPECT_FALSE(hasP99);
    EXPECT_FALSE(hasP999);
    EXPECT_FALSE(hasP9999);
}

TEST(PercentileTest, ZeroTickCount) {
    std::vector<uint64_t> topN = {500, 400};
    uint64_t tickCount = 0;
    uint64_t p99Ns, p999Ns, p9999Ns;
    bool hasP99, hasP999, hasP9999;

    CalculatePercentiles(topN, tickCount, p99Ns, hasP99, p999Ns, hasP999, p9999Ns, hasP9999);

    EXPECT_FALSE(hasP99);
    EXPECT_FALSE(hasP999);
    EXPECT_FALSE(hasP9999);
}

TEST(PercentileTest, LargeTickCount) {
    std::vector<uint64_t> topN = {1000, 900, 800, 700, 600, 500, 400, 300, 200, 100};
    uint64_t tickCount = 10000;
    uint64_t p99Ns, p999Ns, p9999Ns;
    bool hasP99, hasP999, hasP9999;

    CalculatePercentiles(topN, tickCount, p99Ns, hasP99, p999Ns, hasP999, p9999Ns, hasP9999);

    EXPECT_FALSE(hasP99);
    EXPECT_FALSE(hasP999);
    EXPECT_TRUE(hasP9999);
    EXPECT_EQ(900u, p9999Ns);
}

#endif
