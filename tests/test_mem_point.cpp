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
#include "spdiag/mem_point_keys.h"
#include <set>
#include <string>

using namespace SpDiag;

TEST(MemPointKeyTest, EnumValues) {
    EXPECT_EQ(static_cast<uint32_t>(MemPointKey::NONE), 0u);
    EXPECT_EQ(static_cast<uint32_t>(MemPointKey::RPC_REQUEST_BUF), 1u);
    EXPECT_EQ(static_cast<uint32_t>(MemPointKey::RPC_RESPONSE_BUF), 2u);
    EXPECT_EQ(static_cast<uint32_t>(MemPointKey::SQL_PLAN_CACHE), 3u);
    EXPECT_EQ(static_cast<uint32_t>(MemPointKey::SQL_RESULT_SET), 4u);
    EXPECT_EQ(static_cast<uint32_t>(MemPointKey::SESSION_CONN_CTX), 5u);
    EXPECT_EQ(static_cast<uint32_t>(MemPointKey::TX_UNDO_BUFFER), 6u);
    EXPECT_EQ(static_cast<uint32_t>(MemPointKey::LOG_WRITE_BUFFER), 7u);
    EXPECT_EQ(static_cast<uint32_t>(MemPointKey::TX_EXEC_CTX), 8u);
    EXPECT_EQ(static_cast<uint32_t>(MemPointKey::NET_SEND_BUF), 9u);
    EXPECT_EQ(static_cast<uint32_t>(MemPointKey::ASYNC_TASK_CTX), 10u);
    EXPECT_EQ(static_cast<uint32_t>(MemPointKey::SQL_VECTOR_GROW), 11u);
    EXPECT_EQ(static_cast<uint32_t>(MemPointKey::LOG_STRING_SSO), 12u);
    EXPECT_EQ(static_cast<uint32_t>(MemPointKey::CONN_MAP_CHURN), 13u);
    EXPECT_EQ(static_cast<uint32_t>(MemPointKey::RPC_SPIKE), 14u);
    EXPECT_EQ(static_cast<uint32_t>(MemPointKey::SQL_NESTED_VEC), 15u);
    EXPECT_EQ(static_cast<uint32_t>(MemPointKey::SQL_NESTED_STR), 16u);
    EXPECT_EQ(static_cast<uint32_t>(MemPointKey::SQL_REALLOC), 17u);
}

TEST(MemPointKeyTest, SentinelValue) {
    constexpr uint32_t count = static_cast<uint32_t>(MemPointKey::MEM_POINT_KEY_COUNT);
    EXPECT_EQ(count, 18u);
}

TEST(MemPointKeyTest, KeyCount) {
    EXPECT_EQ(MemPointKeyCount(), 17u);
    EXPECT_EQ(MemPointKeyCount(),
              static_cast<uint32_t>(MemPointKey::MEM_POINT_KEY_COUNT) - 1);
}

TEST(MemPointKeyTest, CategoryArrays) {
    EXPECT_STREQ(memPointKeyCategories[0], "Rpc");
    EXPECT_STREQ(memPointKeyCategories[1], "Rpc");
    EXPECT_STREQ(memPointKeyCategories[2], "Sql");
    EXPECT_STREQ(memPointKeyCategories[3], "Sql");
    EXPECT_STREQ(memPointKeyCategories[4], "Connection");
    EXPECT_STREQ(memPointKeyCategories[5], "Transaction");
    EXPECT_STREQ(memPointKeyCategories[6], "Log");
    EXPECT_STREQ(memPointKeyCategories[7], "Execution");
    EXPECT_STREQ(memPointKeyCategories[8], "Net");
    EXPECT_STREQ(memPointKeyCategories[9], "Async");
    EXPECT_STREQ(memPointKeyCategories[10], "Sql");
    EXPECT_STREQ(memPointKeyCategories[11], "Log");
    EXPECT_STREQ(memPointKeyCategories[12], "Connection");
    EXPECT_STREQ(memPointKeyCategories[13], "Rpc");
    EXPECT_STREQ(memPointKeyCategories[14], "Sql");
    EXPECT_STREQ(memPointKeyCategories[15], "Sql");
    EXPECT_STREQ(memPointKeyCategories[16], "Sql");
}

TEST(MemPointKeyTest, DescriptionArrays) {
    EXPECT_STREQ(memPointKeyDescriptions[0], "Stack_value");
    EXPECT_STREQ(memPointKeyDescriptions[3], "Struct_member");
    EXPECT_STREQ(memPointKeyDescriptions[6], "Tracked_string");
    EXPECT_STREQ(memPointKeyDescriptions[7], "Heap_object");
    EXPECT_STREQ(memPointKeyDescriptions[9], "Custom_lifecycle");
    EXPECT_STREQ(memPointKeyDescriptions[10], "Vector_growth");
    EXPECT_STREQ(memPointKeyDescriptions[11], "String_sso");
    EXPECT_STREQ(memPointKeyDescriptions[12], "Map_churn");
    EXPECT_STREQ(memPointKeyDescriptions[13], "Spike");
    EXPECT_STREQ(memPointKeyDescriptions[14], "Nested_vec");
    EXPECT_STREQ(memPointKeyDescriptions[15], "Nested_str");
    EXPECT_STREQ(memPointKeyDescriptions[16], "Realloc_cascade");
}

TEST(MemPointKeyTest, GetCategory) {
    EXPECT_STREQ(GetMemPointKeyCategory(MemPointKey::RPC_REQUEST_BUF), "Rpc");
    EXPECT_STREQ(GetMemPointKeyCategory(MemPointKey::SQL_PLAN_CACHE), "Sql");
    EXPECT_STREQ(GetMemPointKeyCategory(MemPointKey::TX_UNDO_BUFFER), "Transaction");
    EXPECT_STREQ(GetMemPointKeyCategory(MemPointKey::TX_EXEC_CTX), "Execution");
    EXPECT_STREQ(GetMemPointKeyCategory(MemPointKey::SQL_VECTOR_GROW), "Sql");
    EXPECT_STREQ(GetMemPointKeyCategory(MemPointKey::RPC_SPIKE), "Rpc");
    EXPECT_STREQ(GetMemPointKeyCategory(MemPointKey::NONE), "unknown");
}

TEST(MemPointKeyTest, GetDescription) {
    EXPECT_STREQ(GetMemPointKeyDescription(MemPointKey::RPC_REQUEST_BUF),
                 "Stack_value");
    EXPECT_STREQ(GetMemPointKeyDescription(MemPointKey::SQL_VECTOR_GROW),
                 "Vector_growth");
    EXPECT_STREQ(GetMemPointKeyDescription(MemPointKey::NONE), "");
}

TEST(MemPointKeyTest, GetCategoryOutOfRange) {
    auto beyondLast = static_cast<MemPointKey>(
        static_cast<uint32_t>(MemPointKey::MEM_POINT_KEY_COUNT));
    EXPECT_STREQ(GetMemPointKeyCategory(beyondLast), "unknown");
    EXPECT_STREQ(GetMemPointKeyCategory(static_cast<MemPointKey>(255)), "unknown");
}

TEST(MemPointKeyTest, GetDescriptionOutOfRange) {
    auto beyondLast = static_cast<MemPointKey>(
        static_cast<uint32_t>(MemPointKey::MEM_POINT_KEY_COUNT));
    EXPECT_STREQ(GetMemPointKeyDescription(beyondLast), "");
    EXPECT_STREQ(GetMemPointKeyDescription(static_cast<MemPointKey>(255)), "");
}

TEST(MemPointKeyTest, CategoriesNonEmpty) {
    for (uint32_t i = 0; i < MemPointKeyCount(); ++i) {
        EXPECT_STRNE(memPointKeyCategories[i], "")
            << "Category at index " << i << " should not be empty";
    }
}

TEST(MemPointKeyTest, DescriptionsNonEmpty) {
    for (uint32_t i = 0; i < MemPointKeyCount(); ++i) {
        EXPECT_STRNE(memPointKeyDescriptions[i], "")
            << "Description at index " << i << " should not be empty";
    }
}

TEST(MemPointKeyTest, EnumValuesUnique) {
    std::set<uint32_t> values;
    values.insert(static_cast<uint32_t>(MemPointKey::NONE));
    values.insert(static_cast<uint32_t>(MemPointKey::RPC_REQUEST_BUF));
    values.insert(static_cast<uint32_t>(MemPointKey::RPC_RESPONSE_BUF));
    values.insert(static_cast<uint32_t>(MemPointKey::SQL_PLAN_CACHE));
    values.insert(static_cast<uint32_t>(MemPointKey::SQL_RESULT_SET));
    values.insert(static_cast<uint32_t>(MemPointKey::SESSION_CONN_CTX));
    values.insert(static_cast<uint32_t>(MemPointKey::TX_UNDO_BUFFER));
    values.insert(static_cast<uint32_t>(MemPointKey::LOG_WRITE_BUFFER));
    values.insert(static_cast<uint32_t>(MemPointKey::TX_EXEC_CTX));
    values.insert(static_cast<uint32_t>(MemPointKey::NET_SEND_BUF));
    values.insert(static_cast<uint32_t>(MemPointKey::ASYNC_TASK_CTX));
    values.insert(static_cast<uint32_t>(MemPointKey::SQL_VECTOR_GROW));
    values.insert(static_cast<uint32_t>(MemPointKey::LOG_STRING_SSO));
    values.insert(static_cast<uint32_t>(MemPointKey::CONN_MAP_CHURN));
    values.insert(static_cast<uint32_t>(MemPointKey::RPC_SPIKE));
    values.insert(static_cast<uint32_t>(MemPointKey::SQL_NESTED_VEC));
    values.insert(static_cast<uint32_t>(MemPointKey::SQL_NESTED_STR));
    values.insert(static_cast<uint32_t>(MemPointKey::SQL_REALLOC));
    values.insert(static_cast<uint32_t>(MemPointKey::MEM_POINT_KEY_COUNT));
    EXPECT_EQ(values.size(), 19u);
}

TEST(MemPointKeyTest, AllKeysRoundTrip) {
    for (uint32_t i = 1; i < static_cast<uint32_t>(MemPointKey::MEM_POINT_KEY_COUNT); ++i) {
        auto key = static_cast<MemPointKey>(i);
        EXPECT_STRNE(GetMemPointKeyCategory(key), "unknown")
            << "Key " << i << " should have a valid category";
        EXPECT_STRNE(GetMemPointKeyDescription(key), "")
            << "Key " << i << " should have a valid description";
    }
}
