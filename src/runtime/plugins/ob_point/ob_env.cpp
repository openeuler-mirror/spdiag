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

#include "ob_env.h"

#include <fstream>

#include "spdiag/log_macros.h"

namespace SpDiag {
namespace ObEnv {

bool CheckLinux(std::string* err) {
    (void)err;
    return true;

}

bool CheckBtf(std::string* err) {
    std::ifstream btf("/sys/kernel/btf/vmlinux", std::ios::binary);
    if (!btf.is_open()) {
        if (err) *err = "BTF not available: /sys/kernel/btf/vmlinux missing";
        SPDIAG_LOG_ERROR(LogModule::PLATFORM, "BTF not available: /sys/kernel/btf/vmlinux missing");
        return false;
    }
    return true;

}

bool CheckPerfEventParanoid(std::string* err) {
    std::ifstream paranoid("/proc/sys/kernel/perf_event_paranoid");
    int value = 3;
    if (paranoid.is_open() && (paranoid >> value) && value > 2) {
        if (err) *err = "perf_event_paranoid too high: " + std::to_string(value) + " (need <= 2)";
        SPDIAG_LOG_ERROR(LogModule::PLATFORM, "perf_event_paranoid too high: %d (need <= 2)", value);
        return false;
    }
    return true;

}

bool CheckAll(std::string* err) {
    if (!CheckLinux(err)) return false;
    if (!CheckBtf(err)) return false;
    if (!CheckPerfEventParanoid(err)) return false;
    return true;
}

}  // namespace ObEnv
}  // namespace SpDiag
