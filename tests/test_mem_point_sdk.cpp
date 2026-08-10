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

#ifdef SPDIAG_ENABLE_MEMPOINT

#include "spdiag/mem_point.h"
#include "spdiag/tracked_allocator.h"
#include "spdiag/tracked_containers.h"

using namespace SpDiag;

TEST(MemPointSdkTest, TrackValueConstruct) {
    char buf[1024]{};
    auto mp = MemPoint::TrackValue(MemPointKey::RPC_REQUEST_BUF, buf, sizeof(buf));
    EXPECT_TRUE(mp.TestIsActive());
    EXPECT_EQ(mp.TestGetId(), static_cast<uint32_t>(MemPointKey::RPC_REQUEST_BUF));
    EXPECT_EQ(mp.TestGetSize(), 1024u);
    EXPECT_EQ(mp.TestGetObj(), buf);
}

TEST(MemPointSdkTest, TrackObjectTemplate) {
    struct Payload { uint64_t a, b, c; };
    Payload p{};
    auto mp = MemPoint::TrackObject(MemPointKey::SESSION_CONN_CTX, p);
    EXPECT_EQ(mp.TestGetId(), static_cast<uint32_t>(MemPointKey::SESSION_CONN_CTX));
    EXPECT_EQ(mp.TestGetSize(), sizeof(Payload));
    EXPECT_EQ(mp.TestGetObj(), &p);
}

TEST(MemPointSdkTest, TrackObjectWithExtra) {
    struct Header { int x; };
    Header h{};
    constexpr uint64_t extraBytes = 4096;
    auto mp = MemPoint::TrackObject(MemPointKey::SQL_RESULT_SET, h, extraBytes);
    EXPECT_EQ(mp.TestGetSize(), sizeof(Header) + extraBytes);
}

TEST(MemPointSdkTest, TrackBufferConstruct) {
    void* ptr = reinterpret_cast<void*>(0xDEADBEEF);
    auto mp = MemPoint::TrackBuffer(MemPointKey::TX_UNDO_BUFFER, ptr, 8192);
    EXPECT_EQ(mp.TestGetId(), static_cast<uint32_t>(MemPointKey::TX_UNDO_BUFFER));
    EXPECT_EQ(mp.TestGetSize(), 8192u);
    EXPECT_EQ(mp.TestGetObj(), ptr);
}

TEST(MemPointSdkTest, MoveConstructorTransfersActive) {
    char buf[64]{};
    auto mp1 = MemPoint::TrackValue(MemPointKey::LOG_WRITE_BUFFER, buf, 64);
    EXPECT_TRUE(mp1.TestIsActive());

    auto mp2 = std::move(mp1);
    EXPECT_TRUE(mp2.TestIsActive());
    EXPECT_FALSE(mp1.TestIsActive());
    EXPECT_EQ(mp2.TestGetId(), static_cast<uint32_t>(MemPointKey::LOG_WRITE_BUFFER));
    EXPECT_EQ(mp2.TestGetSize(), 64u);
}

TEST(MemPointSdkTest, MoveConstructorSourceSafeDestruct) {
    MemPoint mp1 = MemPoint::TrackValue(MemPointKey::RPC_REQUEST_BUF, nullptr, 0);
    {
        auto mp2 = std::move(mp1);
    }
    EXPECT_FALSE(mp1.TestIsActive());
}

TEST(MemPointSdkTest, MoveAssignmentTransfersActive) {
    auto mp1 = MemPoint::TrackValue(MemPointKey::RPC_REQUEST_BUF, nullptr, 100);
    auto mp2 = MemPoint::TrackValue(MemPointKey::RPC_RESPONSE_BUF, nullptr, 200);
    EXPECT_TRUE(mp1.TestIsActive());
    EXPECT_TRUE(mp2.TestIsActive());

    mp2 = std::move(mp1);
    EXPECT_FALSE(mp1.TestIsActive());
    EXPECT_TRUE(mp2.TestIsActive());
    EXPECT_EQ(mp2.TestGetId(), static_cast<uint32_t>(MemPointKey::RPC_REQUEST_BUF));
    EXPECT_EQ(mp2.TestGetSize(), 100u);
}

TEST(MemPointSdkTest, SelfMoveAssignmentSafe) {
    auto mp = MemPoint::TrackValue(MemPointKey::SQL_PLAN_CACHE, nullptr, 42);
    auto* addr = &mp;
    mp = std::move(*addr);
    EXPECT_TRUE(mp.TestIsActive());
    EXPECT_EQ(mp.TestGetSize(), 42u);
}

TEST(MemPointSdkTest, UpdateSizeChangesSize) {
    auto mp = MemPoint::TrackValue(MemPointKey::SQL_RESULT_SET, nullptr, 100);
    EXPECT_EQ(mp.TestGetSize(), 100u);

    mp.UpdateSize(200);
    EXPECT_EQ(mp.TestGetSize(), 200u);

    mp.UpdateSize(50);
    EXPECT_EQ(mp.TestGetSize(), 50u);
}

TEST(MemPointSdkTest, UpdateSizeMultipleCalls) {
    auto mp = MemPoint::TrackValue(MemPointKey::TX_UNDO_BUFFER, nullptr, 0);
    for (uint64_t i = 0; i < 1000; ++i) {
        mp.UpdateSize(i * 64);
    }
    EXPECT_EQ(mp.TestGetSize(), 999u * 64);
}

TEST(MemPointSdkTest, ScopeEndDestructs) {
    {
        auto mp = MemPoint::TrackValue(MemPointKey::LOG_WRITE_BUFFER, nullptr, 16);
        EXPECT_TRUE(mp.TestIsActive());
    }
}

// --- TrackedAllocator tests ---

TEST(MemPointSdkTest, TrackedAllocatorAllocateDeallocate) {
    TrackedAllocator<int, MemPointKey::SQL_PLAN_CACHE> alloc;
    int* p = alloc.allocate(100);
    EXPECT_NE(p, nullptr);
    alloc.deallocate(p, 100);
}

TEST(MemPointSdkTest, TrackedAllocatorRebind) {
    typename TrackedAllocator<int, MemPointKey::SQL_PLAN_CACHE>::template rebind<double>::other reb;
    double* p = reb.allocate(10);
    EXPECT_NE(p, nullptr);
    reb.deallocate(p, 10);
}

TEST(MemPointSdkTest, TrackedAllocatorEquality) {
    TrackedAllocator<int, MemPointKey::SQL_PLAN_CACHE> a;
    TrackedAllocator<double, MemPointKey::SQL_PLAN_CACHE> b;
    EXPECT_TRUE(a == b);
}

TEST(MemPointSdkTest, TrackedVectorPushBack) {
    tracked::vector<int, MemPointKey::SQL_RESULT_SET> vec;
    EXPECT_EQ(vec.size(), 0u);

    vec.reserve(100);
    for (int i = 0; i < 100; ++i) {
        vec.push_back(i);
    }
    EXPECT_EQ(vec.size(), 100u);
    EXPECT_GE(vec.capacity(), 100u);

    vec.clear();
    EXPECT_EQ(vec.size(), 0u);
    EXPECT_GT(vec.capacity(), 0u);
}

TEST(MemPointSdkTest, TrackedStringAppend) {
    tracked::string<MemPointKey::RPC_REQUEST_BUF> str("hello");
    EXPECT_EQ(str.size(), 5u);
    EXPECT_GE(str.capacity(), 5u);

    str.append(" world, this is a longer string");
    EXPECT_GT(str.size(), 5u);

    str.clear();
    EXPECT_EQ(str.size(), 0u);
}

TEST(MemPointSdkTest, TrackedUnorderedMapInsert) {
    tracked::unordered_map<int, int, MemPointKey::SQL_PLAN_CACHE> map;
    EXPECT_TRUE(map.empty());

    for (int i = 0; i < 50; ++i) {
        map[i] = i * 10;
    }
    EXPECT_EQ(map.size(), 50u);
    EXPECT_GT(map.bucket_count(), 0u);

    int erased = static_cast<int>(map.erase(25));
    EXPECT_EQ(erased, 1);
    EXPECT_EQ(map.size(), 49u);
}

#else

TEST(MemPointSdkTest, DisabledOnNonLinux) {
    GTEST_SKIP() << "MemPoint SDK tests require ENABLE_MEMPOINT=ON on Linux";
}

#endif // SPDIAG_ENABLE_MEMPOINT
