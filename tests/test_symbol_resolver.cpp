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
#include <unistd.h>

#include "src/runtime/symbol_resolver.h"

namespace {
void SomeLocalFunction() {}
}  // namespace

TEST(SymbolResolverTest, RefreshAndResolveStackAreSafe) {
    SpDiag::SymbolResolver resolver;

    int pid = 1;
    bool refreshed = resolver.RefreshMaps(pid);
    (void)refreshed;

    std::vector<uint64_t> stack = {0x1, 0x2, 0x3};
    auto symbols = resolver.ResolveStack(pid, stack);
    (void)symbols;
}

TEST(SymbolResolverTest, RefreshMapsOnSelfProcess) {
    SpDiag::SymbolResolver resolver;

    const int32_t pid = static_cast<int32_t>(getpid());
    EXPECT_TRUE(resolver.RefreshMaps(pid));

    // The current process always has a non-empty maps file on Linux.
    const auto& maps = resolver.GetCachedMaps(pid);
    EXPECT_FALSE(maps.empty());
}

TEST(SymbolResolverTest, ResolveStackWithRealAddressIsSafe) {
    SpDiag::SymbolResolver resolver;

    const int32_t pid = static_cast<int32_t>(getpid());
    ASSERT_TRUE(resolver.RefreshMaps(pid));

    std::vector<uint64_t> stack;
    stack.push_back(reinterpret_cast<uintptr_t>(&SomeLocalFunction));

    auto symbols = resolver.ResolveStack(pid, stack);
    // The call must be safe and never return more frames than requested.
    EXPECT_LE(symbols.size(), stack.size());
    // When a frame is resolved, its symbol name is populated.
    for (const auto& sym : symbols) {
        EXPECT_FALSE(sym.symbolName.empty());
    }
}

TEST(SymbolResolverTest, SetRefreshIntervalThenRefreshStillWorks) {
    SpDiag::SymbolResolver resolver;
    resolver.SetRefreshInterval(std::chrono::seconds(1));

    const int32_t pid = static_cast<int32_t>(getpid());
    EXPECT_TRUE(resolver.RefreshMaps(pid));

    // Changing the interval between refreshes must not break subsequent calls.
    resolver.SetRefreshInterval(std::chrono::seconds(60));
    EXPECT_TRUE(resolver.RefreshMaps(pid));
}
