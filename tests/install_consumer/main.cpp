// Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
// SpDiag is licensed under Mulan PSL v2.
#include <spdiag/perf.h>

int main() {
    SpDiag::PerfPoint point(SpDiag::PerfKeyDefined::COMPUTE_MATMUL);
    point.Start();
    point.End(0);
    return 0;
}
