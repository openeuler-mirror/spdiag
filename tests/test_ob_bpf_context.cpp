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

// ObBpfContext exercises that do NOT trigger a real BpfLoader::LoadAndAttach
// (which needs root + a real target). We only validate the non-loaded state
// and lifetime safety here; real load is covered by integration tests.

#ifdef SPDIAG_USES_BPF
#include "src/runtime/plugins/ob_point/ob_bpf_context.h"
#endif

#if defined(SPDIAG_USES_BPF)

TEST(ObBpfContextTest, EmptyContextReturnsNullAccessors) {
    SpDiag::ObBpfContext ctx(0, "/lib/x86_64-linux-gnu/libc.so.6");
    EXPECT_EQ(ctx.GetBpfObject(), nullptr);
    EXPECT_EQ(ctx.GetMapFd("agg"), -1);
    EXPECT_EQ(ctx.GetMapFd("stacks"), -1);
}

TEST(ObBpfContextTest, DestructorSafeWithoutAcquire) {
    // No Acquire call: destructor must not crash and must not unload anything.
    {
        SpDiag::ObBpfContext ctx(0, "/lib/x86_64-linux-gnu/libc.so.6");
    }
    EXPECT_TRUE(true);
}

#endif
