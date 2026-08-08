/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
 * SpDiag is licensed under Mulan PSL v2.
 */

#include "spdiag/perf_point.h"

#include <cstdint>
#include <type_traits>

enum class McPerfKey : uint32_t {
    TRANSFER_WRITE = 1,
};

static_assert(std::is_empty_v<SpDiag::PerfPoint>);
static_assert(std::is_trivially_destructible_v<SpDiag::PerfPoint>);
static_assert(std::is_nothrow_constructible_v<SpDiag::PerfPoint, McPerfKey>);

int main() {
    SpDiag::PerfPoint pt(McPerfKey::TRANSFER_WRITE);
    pt.Start();
    pt.End(0);
    return 0;
}
