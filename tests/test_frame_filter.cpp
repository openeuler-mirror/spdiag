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
#include "src/cli/frame_filter.h"

using namespace SpDiag;

TEST(FrameFilterTest, IsMappingSymbol) {
    EXPECT_TRUE(FrameFilter::IsMappingSymbol("$x"));
    EXPECT_TRUE(FrameFilter::IsMappingSymbol("$d"));
    EXPECT_TRUE(FrameFilter::IsMappingSymbol("$a"));
    EXPECT_TRUE(FrameFilter::IsMappingSymbol("$t"));
    EXPECT_TRUE(FrameFilter::IsMappingSymbol("$."));
    EXPECT_TRUE(FrameFilter::IsMappingSymbol("$xfoo"));
    EXPECT_FALSE(FrameFilter::IsMappingSymbol("main"));
    EXPECT_FALSE(FrameFilter::IsMappingSymbol("$"));
    EXPECT_FALSE(FrameFilter::IsMappingSymbol(""));
    EXPECT_FALSE(FrameFilter::IsMappingSymbol("$M"));
    EXPECT_FALSE(FrameFilter::IsMappingSymbol("$_x"));
}

TEST(FrameFilterTest, IsStlGlue) {
    EXPECT_TRUE(FrameFilter::IsStlGlue("std::_Vector_base<int, std::allocator<int>>"));
    EXPECT_TRUE(FrameFilter::IsStlGlue("std::__new_allocator<int>"));
    EXPECT_TRUE(FrameFilter::IsStlGlue("std::__uninitialized_copy"));
    EXPECT_TRUE(FrameFilter::IsStlGlue("std::_Construct<int>"));
    EXPECT_TRUE(FrameFilter::IsStlGlue("std::allocator_traits<std::allocator<int>>"));
    EXPECT_TRUE(FrameFilter::IsStlGlue("std::thread::_Invoker<std::tuple<>>"));
    EXPECT_TRUE(FrameFilter::IsStlGlue("std::__cxx11::basic_string<char>"));
    EXPECT_FALSE(FrameFilter::IsStlGlue("my_app::do_work"));
    EXPECT_FALSE(FrameFilter::IsStlGlue("malloc"));
    EXPECT_FALSE(FrameFilter::IsStlGlue("main"));
}

TEST(FrameFilterTest, IsThreadTrampoline) {
    EXPECT_TRUE(FrameFilter::IsThreadTrampoline("start_thread"));
    EXPECT_TRUE(FrameFilter::IsThreadTrampoline("clone3"));
    EXPECT_TRUE(FrameFilter::IsThreadTrampoline("clone"));
    EXPECT_TRUE(FrameFilter::IsThreadTrampoline("__clone"));
    EXPECT_TRUE(FrameFilter::IsThreadTrampoline("[thread entry]"));
    EXPECT_TRUE(FrameFilter::IsThreadTrampoline("std::thread::_Invoker<std::tuple<>>"));
    EXPECT_FALSE(FrameFilter::IsThreadTrampoline("main"));
    EXPECT_FALSE(FrameFilter::IsThreadTrampoline("my_function"));
}

TEST(FrameFilterTest, IsAllocatorFunc) {
    EXPECT_TRUE(FrameFilter::IsAllocatorFunc("operator new"));
    EXPECT_TRUE(FrameFilter::IsAllocatorFunc("operator new[]"));
    EXPECT_TRUE(FrameFilter::IsAllocatorFunc("operator delete"));
    EXPECT_TRUE(FrameFilter::IsAllocatorFunc("malloc"));
    EXPECT_TRUE(FrameFilter::IsAllocatorFunc("calloc"));
    EXPECT_TRUE(FrameFilter::IsAllocatorFunc("realloc"));
    EXPECT_TRUE(FrameFilter::IsAllocatorFunc("free"));
    EXPECT_FALSE(FrameFilter::IsAllocatorFunc("my_func"));
    EXPECT_FALSE(FrameFilter::IsAllocatorFunc("operator<<"));
}

TEST(FrameFilterTest, IsLibraryModule) {
    EXPECT_TRUE(FrameFilter::IsLibraryModule("libc.so.6"));
    EXPECT_TRUE(FrameFilter::IsLibraryModule("/usr/lib/libstdc++.so.6"));
    EXPECT_TRUE(FrameFilter::IsLibraryModule("ld-linux-x86-64.so.2"));
    EXPECT_FALSE(FrameFilter::IsLibraryModule("my_app"));
    EXPECT_FALSE(FrameFilter::IsLibraryModule("libmylib.so"));
}

TEST(FrameFilterTest, ApplyDropsMappingSymbols) {
    FrameFilter filter;
    std::vector<FrameInfo> frames;
    frames.push_back({0x1000, "main", "app", "", 0});
    frames.push_back({0x2000, "$x", "app", "", 0});
    frames.push_back({0x3000, "helper", "app", "", 0});

    auto result = filter.Apply(frames);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].func, "main");
    EXPECT_EQ(result[1].func, "helper");
}

TEST(FrameFilterTest, ApplyCollapsesAdjacentDuplicates) {
    FrameFilter filter;
    std::vector<FrameInfo> frames;
    frames.push_back({0x1000, "funcA", "app", "", 10});
    frames.push_back({0x1000, "funcA", "app", "", 10});
    frames.push_back({0x2000, "funcB", "app", "", 20});

    auto result = filter.Apply(frames);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].func, "funcA");
    EXPECT_EQ(result[1].func, "funcB");
}

TEST(FrameFilterTest, ApplyCollapsesThreadTrampoline) {
    FrameFilter filter;
    std::vector<FrameInfo> frames;
    frames.push_back({0x1000, "start_thread", "libc.so.6", "", 0});
    frames.push_back({0x2000, "clone3", "libc.so.6", "", 0});
    frames.push_back({0x3000, "worker_func", "app", "", 0});

    auto result = filter.Apply(frames);
    bool foundThreadEntry = false;
    for (const auto& f : result) {
        if (f.func == "[thread entry]") foundThreadEntry = true;
    }
    EXPECT_TRUE(foundThreadEntry);
    EXPECT_GE(result.size(), 2u);
}

TEST(FrameFilterTest, PickHeaderFrame) {
    std::vector<FrameInfo> frames;
    frames.push_back({0x1000, "operator new", "app", "", 0});
    frames.push_back({0x2000, "my_app::alloc", "app", "", 0});
    frames.push_back({0x3000, "std::vector", "app", "", 0});

    auto picked = FrameFilter::PickHeaderFrame(frames);
    EXPECT_EQ(picked, "my_app::alloc");
}

TEST(FrameFilterTest, PickFrameSummary) {
    std::vector<FrameInfo> frames;
    frames.push_back({0x1000, "malloc", "libc.so.6", "", 0});
    frames.push_back({0x2000, "my_func", "app", "", 0});
    frames.push_back({0x3000, "std::thread::_Invoker<...>", "app", "", 0});

    const FrameInfo* picked = FrameFilter::PickFrameSummary(frames);
    ASSERT_NE(picked, nullptr);
    EXPECT_EQ(picked->func, "my_func");
}

TEST(FrameFilterTest, IsExactSymbol) {
    EXPECT_TRUE(FrameFilter::IsExactSymbol("malloc", "malloc"));
    EXPECT_TRUE(FrameFilter::IsExactSymbol("malloc(unsigned long)", "malloc"));
    EXPECT_FALSE(FrameFilter::IsExactSymbol("malloc_x", "malloc"));
    EXPECT_FALSE(FrameFilter::IsExactSymbol("my_malloc", "malloc"));
}
