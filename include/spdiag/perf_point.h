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
#include <cstdint>
#include <chrono>
#include <type_traits>
#include <atomic>
#include "spdiag/perf_level.h"
#include "spdiag/shm_layout.h"

namespace SpDiag {

// 全局 PerfPoint 的 tag。PerfPoint(KEY) 为本地点（默认）；PerfPoint(KEY, global_perf) 为全局点。
struct global_perf_t {};
inline constexpr global_perf_t global_perf{};

class PerfPoint {
public:
#ifdef SPDIAG_DISABLE
    // Compile-time disabled implementation. Keep the public call syntax
    // unchanged while allowing the compiler to remove the empty object and
    // all of its operations.
    constexpr explicit PerfPoint(uint32_t, PerfLevel) noexcept {}

    template<typename E, typename = std::enable_if_t<std::is_enum_v<E>>>
    constexpr explicit PerfPoint(
        E, PerfLevel = PerfLevel::SUB_SYSTEM) noexcept {}

    constexpr explicit PerfPoint(
        uint32_t, global_perf_t,
        PerfLevel = PerfLevel::SUB_SYSTEM) noexcept {}

    template<typename E, typename = std::enable_if_t<std::is_enum_v<E>>>
    constexpr explicit PerfPoint(
        E, global_perf_t,
        PerfLevel = PerfLevel::SUB_SYSTEM) noexcept {}

    constexpr void Start() noexcept {}
    constexpr void End(int = 0) noexcept {}
    constexpr void Abandon() noexcept {}
#else
    // 核心构造函数：接受 uint32_t 槽位号（库内部使用）—— 本地
    explicit PerfPoint(uint32_t slotIndex, PerfLevel level);

    // 模板构造函数：接受任意枚举类型，编译期 static_cast 转发（零开销）—— 本地默认
    template<typename E, typename = std::enable_if_t<std::is_enum_v<E>>>
    explicit PerfPoint(E key, PerfLevel level = PerfLevel::SUB_SYSTEM)
        : PerfPoint(static_cast<uint32_t>(key), level) {}

    // —— 全局 PerfPoint 重载（跨函数/跨线程安全，累加进工厂类共享原子累加器）——
    // 核心构造：uint32_t 槽位号 + global_perf tag
    explicit PerfPoint(uint32_t slotIndex, global_perf_t, PerfLevel level = PerfLevel::SUB_SYSTEM);
    // 模板构造：任意枚举 + global_perf tag
    template<typename E, typename = std::enable_if_t<std::is_enum_v<E>>>
    explicit PerfPoint(E key, global_perf_t, PerfLevel level = PerfLevel::SUB_SYSTEM)
        : PerfPoint(static_cast<uint32_t>(key), global_perf, level) {}

    ~PerfPoint();

    void Start();
    void End(int returnCode = 0);
    void Abandon();

private:
    // —— 本地（原样保留，零开销热路径不动）——
    uint32_t  slotIndex_ = UINT32_MAX;
    PerfLevel level_ = PerfLevel::SUB_SYSTEM;
    uint64_t  startTs_ = 0;
    uint64_t  nsPerTick_ = 0;
    bool      ended_ = true;
    bool      active_ = false;
    PerfStat* stat_ = nullptr;
    // —— 全局（仅 global 模式使用）——
    bool                 global_ = false;
    std::atomic<uint64_t> startTsGlobal_{0};   // 跨线程 handoff：Start release / End acquire
    GlobalAccum*         gaccum_ = nullptr;    // 指向工厂类累加器（非拥有）
#endif
};

}
