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
#include "src/cli/display_engine.h"
#include "src/runtime/data_pipeline.h"
#include <sstream>

using namespace SpDiag;

TEST(CacheDisplayTest, CumulativeMode) {
    DisplayEngine engine;
    std::vector<ProcessorOutput> outputs;
    outputs.push_back({0, "", "", "cache.L1-dcache.references", "1000"});
    outputs.push_back({0, "", "", "cache.L1-dcache.misses", "100"});

    std::ostringstream oss;
    auto oldBuf = std::cout.rdbuf(oss.rdbuf());

    engine.ShowObCache(outputs, true);

    std::cout.rdbuf(oldBuf);
    std::string output = oss.str();

    EXPECT_TRUE(output.find("References") != std::string::npos);
    EXPECT_TRUE(output.find("Misses") != std::string::npos);
    EXPECT_TRUE(output.find("MissRate") != std::string::npos);
    EXPECT_TRUE(output.find("HitRate") != std::string::npos);
    EXPECT_TRUE(output.find("1,000") != std::string::npos);
    EXPECT_TRUE(output.find("100") != std::string::npos);
    EXPECT_TRUE(output.find("10.00") != std::string::npos);
    EXPECT_TRUE(output.find("90.00") != std::string::npos);
}

TEST(CacheDisplayTest, DeltaMode) {
    DisplayEngine engine;

    std::vector<ProcessorOutput> outputs1;
    outputs1.push_back({0, "", "", "cache.L1-dcache.references", "1000"});
    outputs1.push_back({0, "", "", "cache.L1-dcache.misses", "100"});

    std::ostringstream oss1;
    auto oldBuf = std::cout.rdbuf(oss1.rdbuf());
    engine.ShowObCache(outputs1, false);
    std::cout.rdbuf(oldBuf);

    std::vector<ProcessorOutput> outputs2;
    outputs2.push_back({0, "", "", "cache.L1-dcache.references", "1500"});
    outputs2.push_back({0, "", "", "cache.L1-dcache.misses", "200"});

    std::ostringstream oss2;
    oldBuf = std::cout.rdbuf(oss2.rdbuf());
    engine.ShowObCache(outputs2, false);
    std::cout.rdbuf(oldBuf);

    EXPECT_TRUE(oss1.str().find("1,000") != std::string::npos);
    EXPECT_TRUE(oss1.str().find("100") != std::string::npos);
    EXPECT_TRUE(oss2.str().find("1,500") != std::string::npos);
    EXPECT_TRUE(oss2.str().find("200") != std::string::npos);
}

TEST(CacheDisplayTest, MissRateHitRate) {
    DisplayEngine engine;
    std::vector<ProcessorOutput> outputs;
    outputs.push_back({0, "", "", "cache.L1-dcache.references", "200"});
    outputs.push_back({0, "", "", "cache.L1-dcache.misses", "100"});

    std::ostringstream oss;
    auto oldBuf = std::cout.rdbuf(oss.rdbuf());

    engine.ShowObCache(outputs, true);

    std::cout.rdbuf(oldBuf);
    std::string output = oss.str();

    EXPECT_TRUE(output.find("50.00") != std::string::npos);
}

TEST(CacheDisplayTest, EmptyOutput) {
    DisplayEngine engine;
    std::vector<ProcessorOutput> outputs;

    std::ostringstream oss;
    auto oldBuf = std::cout.rdbuf(oss.rdbuf());

    engine.ShowObCache(outputs, true);

    std::cout.rdbuf(oldBuf);
    std::string output = oss.str();

    EXPECT_TRUE(output.find("References") != std::string::npos);
    EXPECT_TRUE(output.find("Misses") != std::string::npos);
}
