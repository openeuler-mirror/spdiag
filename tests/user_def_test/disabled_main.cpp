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

#define SPDIAG_PROGRAM_NAME "user_def_test_disabled"
#include "spdiag/auto_perf.h"

#include <iostream>

int main() {
    SpDiag::PerfPoint point(
        PerfKey::USER_LOGIN,
        SpDiag::PerfLevel::KEY_MODULE);
    point.Start();
    int rc = 0;
    point.End(rc);

    std::cout << "[PASS] SPDIAG_DISABLE keeps the PerfPoint call syntax\n";
    return rc;
}
