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

#include "spdiag/field_size_traits.h"
#include "spdiag/object_mem_point.h"
#include <string>
#include <vector>
#include <deque>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

using namespace SpDiag;

// ---------------------------------------------------------------------------
// FieldSizeTraits tests
// ---------------------------------------------------------------------------

TEST(FieldSizeTraitsTest, PlainTypeReturnsZero) {
    EXPECT_EQ(HeapSizeTraits<int>::Get(42), 0u);
}

TEST(FieldSizeTraitsTest, VectorEmptyReturnsZero) {
    std::vector<int> v;
    EXPECT_EQ(GetContainerHeapSize(v), 0u);
}

TEST(FieldSizeTraitsTest, VectorWithCapacity) {
    std::vector<int> v;
    v.reserve(100);
    EXPECT_EQ(GetContainerHeapSize(v), static_cast<uint64_t>(100 * sizeof(int)));
}

TEST(FieldSizeTraitsTest, StringSSO) {
    std::string s = "hi";
    EXPECT_EQ(GetContainerHeapSize(s), 0u);
}

TEST(FieldSizeTraitsTest, StringHeap) {
    std::string s(100, 'x');
    EXPECT_GT(GetContainerHeapSize(s), 0u);
}

TEST(FieldSizeTraitsTest, UnorderedMapEmpty) {
    std::unordered_map<int, int> m;
    EXPECT_EQ(GetContainerHeapSize(m),
              static_cast<uint64_t>(m.bucket_count()) * sizeof(void*)
              + static_cast<uint64_t>(m.size())
                    * sizeof(std::pair<const int, int>));
}

TEST(FieldSizeTraitsTest, MapWithEntries) {
    std::map<int, int> m;
    m[1] = 2;
    m[3] = 4;
    EXPECT_GT(GetContainerHeapSize(m), 0u);
}

TEST(HasHeapDataTest, PlainVsContainer) {
    EXPECT_FALSE(HasHeapData<int>);
    EXPECT_TRUE(HasHeapData<std::vector<int>>);
    EXPECT_TRUE(HasHeapData<std::string>);
    EXPECT_TRUE((HasHeapData<std::unordered_map<int, int>>));
    EXPECT_FALSE(HasHeapData<double>);
}

// ---------------------------------------------------------------------------
// Test structs for ObjectMemPoint tests
// ---------------------------------------------------------------------------

namespace {

struct PlainStruct {
    int32_t count;
    double value;
    uint8_t flag;
};

struct ContainerStruct {
    int32_t id;
    std::vector<int> items;
    std::string name;
    std::map<int, int> scores;
};

struct ContainerClass {
    int32_t id_;
    std::vector<uint8_t> buffer_;
    std::string label_;
    ContainerClass() : id_(42), label_("test") {}
    void AddData(int n) {
        for (int i = 0; i < n; ++i)
            buffer_.push_back(static_cast<uint8_t>(i));
    }
};

struct EmptyStruct {};

struct ManyFieldsStruct {
    int f1;  int f2;  int f3;  int f4;
    int f5;  int f6;  int f7;  int f8;
    int f9;  int f10; int f11; int f12;
    int f13; int f14; int f15; int f16;
};

} // namespace

// ---------------------------------------------------------------------------
// ObjectMemPoint builder tests
// ---------------------------------------------------------------------------

TEST(ObjectMemPointTest, PlainStructFieldRegistration) {
    PlainStruct obj{};
    ObjectMemPoint<PlainStruct> mp("PlainStruct", MemPointKey::SQL_RESULT_SET, obj);
    mp.Field("count", &PlainStruct::count)
      .Field("value", &PlainStruct::value)
      .Field("flag", &PlainStruct::flag);

    EXPECT_EQ(mp.TestGetNumFields(), 3u);
    EXPECT_STREQ(mp.TestGetFieldName(0), "count");
    EXPECT_EQ(mp.TestGetStaticSize(0), static_cast<uint32_t>(sizeof(int32_t)));
    EXPECT_EQ(mp.TestGetStaticSize(1), static_cast<uint32_t>(sizeof(double)));
    EXPECT_EQ(mp.TestGetStaticSize(2), static_cast<uint32_t>(sizeof(uint8_t)));
    EXPECT_EQ(mp.TestGetFieldType(0), 0u); // plain
    EXPECT_EQ(mp.TestCalcHeapSize(0), 0u);  // plain, no heap
}

TEST(ObjectMemPointTest, ContainerStructHeapSizes) {
    ContainerStruct obj;
    obj.items.reserve(50);
    obj.name = "hello world test string that exceeds SSO";

    ObjectMemPoint<ContainerStruct> mp("ContainerStruct",
                                       MemPointKey::SQL_RESULT_SET, obj);
    mp.Field("id", &ContainerStruct::id)
      .ContainerField("items", &ContainerStruct::items)
      .ContainerField("name", &ContainerStruct::name)
      .ContainerField("scores", &ContainerStruct::scores);

    EXPECT_EQ(mp.TestCalcHeapSize(1),
              static_cast<uint64_t>(50 * sizeof(int)));
    EXPECT_GT(mp.TestCalcHeapSize(2), 0u); // exceeds SSO
    EXPECT_EQ(mp.TestCalcHeapSize(3), 0u); // empty map

    // Field type tags: items -> container (1), name -> string (2)
    EXPECT_EQ(mp.TestGetFieldType(1), 1u);
    EXPECT_EQ(mp.TestGetFieldType(2), 2u);
}

TEST(ObjectMemPointTest, ClassWithContainers) {
    ContainerClass obj;
    obj.AddData(100);

    ObjectMemPoint<ContainerClass> mp("ContainerClass",
                                      MemPointKey::SQL_RESULT_SET, obj);
    mp.Field("id_", &ContainerClass::id_)
      .ContainerField("buffer_", &ContainerClass::buffer_)
      .ContainerField("label_", &ContainerClass::label_);

    EXPECT_GT(mp.TestCalcHeapSize(1), 0u);  // 100 elements
    EXPECT_EQ(mp.TestCalcHeapSize(2), 0u);  // SSO, "test" is short
    EXPECT_EQ(mp.TestGetFieldType(0), 0u);  // plain type
}

TEST(ObjectMemPointTest, TypeName) {
    PlainStruct obj{};
    ObjectMemPoint<PlainStruct> mp("PlainStruct",
                                   MemPointKey::SQL_RESULT_SET, obj);
    EXPECT_STREQ(mp.TestGetTypeName(), "PlainStruct");
}

TEST(ObjectMemPointTest, MaxFields) {
    ManyFieldsStruct obj{};
    ObjectMemPoint<ManyFieldsStruct> mp("ManyFieldsStruct",
                                        MemPointKey::SQL_RESULT_SET, obj);
    mp.Field("f1",  &ManyFieldsStruct::f1)
      .Field("f2",  &ManyFieldsStruct::f2)
      .Field("f3",  &ManyFieldsStruct::f3)
      .Field("f4",  &ManyFieldsStruct::f4)
      .Field("f5",  &ManyFieldsStruct::f5)
      .Field("f6",  &ManyFieldsStruct::f6)
      .Field("f7",  &ManyFieldsStruct::f7)
      .Field("f8",  &ManyFieldsStruct::f8)
      .Field("f9",  &ManyFieldsStruct::f9)
      .Field("f10", &ManyFieldsStruct::f10)
      .Field("f11", &ManyFieldsStruct::f11)
      .Field("f12", &ManyFieldsStruct::f12)
      .Field("f13", &ManyFieldsStruct::f13)
      .Field("f14", &ManyFieldsStruct::f14)
      .Field("f15", &ManyFieldsStruct::f15)
      .Field("f16", &ManyFieldsStruct::f16);

    EXPECT_EQ(mp.TestGetNumFields(), 16u);

    // Adding a 17th field should be silently ignored.
    mp.Field("f17", &ManyFieldsStruct::f1);
    EXPECT_EQ(mp.TestGetNumFields(), 16u);
}

TEST(ObjectMemPointTest, MoveSemantics) {
    PlainStruct obj{};
    ObjectMemPoint<PlainStruct> mp1("PlainStruct",
                                    MemPointKey::SQL_RESULT_SET, obj);
    mp1.Field("count", &PlainStruct::count)
       .Field("value", &PlainStruct::value);

    ObjectMemPoint<PlainStruct> mp2(std::move(mp1));

    EXPECT_EQ(mp1.TestGetNumFields(), 0u); // moved-from
    EXPECT_EQ(mp2.TestGetNumFields(), 2u);
}

TEST(ObjectMemPointTest, EmptyStruct) {
    EmptyStruct obj{};
    ObjectMemPoint<EmptyStruct> mp("EmptyStruct",
                                   MemPointKey::SQL_RESULT_SET, obj);
    EXPECT_EQ(mp.TestGetNumFields(), 0u);
}

TEST(ObjectMemPointTest, SnapshotComputesSizes) {
    ContainerStruct obj;
    obj.items = {1, 2, 3, 4, 5};

    ObjectMemPoint<ContainerStruct> mp("ContainerStruct",
                                       MemPointKey::SQL_RESULT_SET, obj);
    mp.Field("id", &ContainerStruct::id)
      .ContainerField("items", &ContainerStruct::items)
      .ContainerField("name", &ContainerStruct::name)
      .ContainerField("scores", &ContainerStruct::scores);

    // Exercises buffer packing and DTRACE_PROBE3. We cannot verify USDT
    // probe output in unit tests, so just confirm it does not crash.
    mp.Snapshot();
}

// ---------------------------------------------------------------------------
// Additional HeapSizeTraits tests
// ---------------------------------------------------------------------------

TEST(FieldSizeTraitsTest, DequeWithElements) {
    std::deque<int> d;
    for (int i = 0; i < 50; ++i) d.push_back(i);
    EXPECT_EQ(GetContainerHeapSize(d), static_cast<uint64_t>(50 * sizeof(int)));
}

TEST(FieldSizeTraitsTest, ListWithElements) {
    std::list<int> l;
    for (int i = 0; i < 10; ++i) l.push_back(i);
    EXPECT_EQ(GetContainerHeapSize(l),
              static_cast<uint64_t>(10 * (sizeof(int) + sizeof(void*) * 2)));
}

TEST(FieldSizeTraitsTest, SetWithEntries) {
    std::set<int> s;
    for (int i = 0; i < 5; ++i) s.insert(i);
    EXPECT_EQ(GetContainerHeapSize(s),
              static_cast<uint64_t>(5 * sizeof(int) + 5 * sizeof(void*) * 3));
}

TEST(FieldSizeTraitsTest, UnorderedSetWithEntries) {
    std::unordered_set<int> s;
    for (int i = 0; i < 5; ++i) s.insert(i);
    EXPECT_EQ(GetContainerHeapSize(s),
              static_cast<uint64_t>(s.bucket_count()) * sizeof(void*)
              + static_cast<uint64_t>(s.size()) * sizeof(int));
}

TEST(FieldSizeTraitsTest, StringSSOBoundary) {
    std::string s15(15, 'x');  // exactly fills SSO buffer
    EXPECT_EQ(GetContainerHeapSize(s15), 0u);

    std::string s16(16, 'x');  // exceeds SSO, heap allocated
    EXPECT_GT(GetContainerHeapSize(s16), 0u);
}

TEST(FieldSizeTraitsTest, EmptyContainerReturnsZero) {
    EXPECT_EQ(HeapSizeTraits<std::deque<int>>::Get(std::deque<int>{}), 0u);
    EXPECT_EQ(HeapSizeTraits<std::list<int>>::Get(std::list<int>{}), 0u);
    EXPECT_EQ(HeapSizeTraits<std::set<int>>::Get(std::set<int>{}), 0u);
    // unordered_set may have non-zero bucket_count even when empty
    {
        std::unordered_set<int> s;
        EXPECT_EQ(GetContainerHeapSize(s),
                  static_cast<uint64_t>(s.bucket_count()) * sizeof(void*));
    }
}

TEST(HasHeapDataTest, AdditionalContainerTypes) {
    EXPECT_TRUE((HasHeapData<std::deque<int>>));
    EXPECT_TRUE((HasHeapData<std::list<int>>));
    EXPECT_TRUE((HasHeapData<std::set<int>>));
    EXPECT_TRUE((HasHeapData<std::unordered_set<int>>));
}

// ---------------------------------------------------------------------------
// Additional ObjectMemPoint tests
// ---------------------------------------------------------------------------

TEST(ObjectMemPointTest, MoveAssignment) {
    PlainStruct obj{};
    ObjectMemPoint<PlainStruct> mp1("PlainStruct",
                                    MemPointKey::SQL_RESULT_SET, obj);
    mp1.Field("count", &PlainStruct::count);

    ObjectMemPoint<PlainStruct> mp2("PlainStruct",
                                    MemPointKey::SQL_RESULT_SET, obj);
    mp2.Field("value", &PlainStruct::value);

    mp2 = std::move(mp1);

    EXPECT_EQ(mp1.TestGetNumFields(), 0u); // moved-from
    EXPECT_EQ(mp2.TestGetNumFields(), 1u);
    EXPECT_STREQ(mp2.TestGetFieldName(0), "count");
}

TEST(ObjectMemPointTest, FieldNameTruncation) {
    PlainStruct obj{};
    ObjectMemPoint<PlainStruct> mp("PlainStruct",
                                   MemPointKey::SQL_RESULT_SET, obj);
    mp.Field("this_is_a_very_long_field_name_exceeding_limit",
             &PlainStruct::count);

    // Name stored internally is the full pointer; truncation happens in
    // Snapshot() when copying to FieldSnapshot.name[16]. Verify the
    // test accessor returns the original name.
    EXPECT_STREQ(mp.TestGetFieldName(0),
                 "this_is_a_very_long_field_name_exceeding_limit");
}

TEST(ObjectMemPointTest, TypeNameTruncation) {
    PlainStruct obj{};
    ObjectMemPoint<PlainStruct> mp(
        "VeryLongTypeNameThatExceedsTheThirtyOneCharLimit",
        MemPointKey::SQL_RESULT_SET, obj);

    // typeName_ buffer is 32 chars; constructor truncates via strncpy(_, _, 31).
    // 结果为输入串的前 31 字符（第 32 字节为 \0）。
    EXPECT_STREQ(mp.TestGetTypeName(),
                 "VeryLongTypeNameThatExceedsTheT");
}

TEST(ObjectMemPointTest, NestedContainerField) {
    struct NestedVec {
        int32_t id;
        std::vector<std::string> rows;
    };

    NestedVec obj{};
    obj.rows.push_back("short");
    obj.rows.push_back(std::string(100, 'x'));

    ObjectMemPoint<NestedVec> mp("NestedVec",
                                 MemPointKey::SQL_RESULT_SET, obj);
    mp.Field("id", &NestedVec::id)
      .ContainerField("rows", &NestedVec::rows);

    EXPECT_EQ(mp.TestGetNumFields(), 2u);
    // Heap size = capacity * sizeof(string), does not include string content
    EXPECT_GT(mp.TestCalcHeapSize(1), 0u);
    EXPECT_EQ(mp.TestGetFieldType(1), 1u); // container
}

TEST(ObjectMemPointTest, SelfMoveAssignmentSafe) {
    PlainStruct obj{};
    ObjectMemPoint<PlainStruct> mp("PlainStruct",
                                   MemPointKey::SQL_RESULT_SET, obj);
    mp.Field("count", &PlainStruct::count);
    auto* addr = &mp;
    mp = std::move(*addr);
    EXPECT_EQ(mp.TestGetNumFields(), 1u);
}

TEST(ObjectMemPointTest, OutOfBoundsAccessors) {
    PlainStruct obj{};
    ObjectMemPoint<PlainStruct> mp("PlainStruct",
                                   MemPointKey::SQL_RESULT_SET, obj);
    mp.Field("count", &PlainStruct::count);

    EXPECT_EQ(mp.TestGetStaticSize(99), 0u);
    EXPECT_STREQ(mp.TestGetFieldName(99), "");
    EXPECT_EQ(mp.TestGetFieldType(99), 0xFF);
    EXPECT_EQ(mp.TestCalcHeapSize(99), 0u);
}

#else // !SPDIAG_ENABLE_MEMPOINT

TEST(ObjectMemPointTest, DisabledOnNonLinux) {
    GTEST_SKIP() << "ObjectMemPoint tests require SPDIAG_ENABLE_MEMPOINT (Linux + systemtap-sdt-dev)";
}

#endif // SPDIAG_ENABLE_MEMPOINT
