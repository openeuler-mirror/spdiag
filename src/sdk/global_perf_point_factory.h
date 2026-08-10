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
#include "spdiag/shm_layout.h"
#include "spdiag/perf_level.h"

namespace SpDiag {

// 全局 PerfPoint 的状态工厂（flyweight/multiton 单例）。
//
// 独占进程内 GlobalAccum[] 原子累加器数组（固定大小 MAX_GLOBAL_POINTS，进程生命期常驻、
// 不释放，故 PerfPoint 缓存的 gaccum_ 指针永不会因 Shutdown 而悬挂）。PerfPoint(global)
// 与 PerfTimer 都只「取用」本工厂，彼此解耦。
//
// 越界（globalSlot >= count_）或未 Init 时 Acquire 返回 nullptr，调用方据此降级为 no-op。
class GlobalPerfPointFactory {
public:
    static GlobalPerfPointFactory& Instance();

    // 程序注册后调用：设定累加器数量并清零。
    void Init(uint32_t maxGlobalPoints);

    // Shutdown / 重 init：count_ 置 0（Acquire 返回 nullptr），数组本身不释放（避免悬挂）。
    void Reset();

    // flyweight：同 key 返回同一累加器；越界或未初始化返回 nullptr。
    GlobalAccum* Acquire(uint32_t globalSlot, PerfLevel level);

    // PerfTimer 快照用：
    GlobalAccum* Array();
    uint32_t     Count() const;

private:
    GlobalPerfPointFactory() = default;
    GlobalAccum accums_[MAX_GLOBAL_POINTS];   // 固定数组，进程生命期常驻
    uint32_t    count_ = 0;
};

}
