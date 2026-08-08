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

#pragma once

#include <string>

namespace SpDiag {

// Shared environment checks for OB observation plugins. Extracted from the
// former SpDiagObPoint::CheckEnvironment so memstat / cachestat / mempoint can
// each validate only what they actually need.
namespace ObEnv {

// Returns false on non-Linux platforms (OB features are Linux-only).
bool CheckLinux(std::string* err = nullptr);

// /sys/kernel/btf/vmlinux must exist (required by BPF CO-RE for memstat/mempoint).
bool CheckBtf(std::string* err = nullptr);

// /proc/sys/kernel/perf_event_paranoid <= 2 (required by perf_event for cachestat).
bool CheckPerfEventParanoid(std::string* err = nullptr);

// CheckLinux && CheckBtf && CheckPerfEventParanoid (memstat/mempoint path).
bool CheckAll(std::string* err = nullptr);

}  // namespace ObEnv

}  // namespace SpDiag
