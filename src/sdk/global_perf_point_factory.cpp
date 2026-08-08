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

#include "global_perf_point_factory.h"
#include <atomic>

namespace SpDiag {

GlobalPerfPointFactory& GlobalPerfPointFactory::Instance() {
    static GlobalPerfPointFactory instance;
    return instance;
}

void GlobalPerfPointFactory::Init(uint32_t maxGlobalPoints) {
    count_ = (maxGlobalPoints <= MAX_GLOBAL_POINTS) ? maxGlobalPoints : MAX_GLOBAL_POINTS;

    // 清零（重 init 场景需要全新起点）。relaxed：单线程 Init，无并发。
    constexpr auto RLX = std::memory_order_relaxed;
    for (uint32_t i = 0; i < count_; ++i) {
        GlobalAccum& a = accums_[i];
        a.goodCount.store(0, RLX);
        a.badCount.store(0, RLX);
        a.notCount.store(0, RLX);
        a.tickCount.store(0, RLX);
        a.totalTimeNs.store(0, RLX);
        a.minTimeNs.store(0, RLX);
        a.maxTimeNs.store(0, RLX);
        a.level.store(0, RLX);
    }
}

void GlobalPerfPointFactory::Reset() {
    count_ = 0;
}

GlobalAccum* GlobalPerfPointFactory::Acquire(uint32_t globalSlot, PerfLevel level) {
    if (globalSlot >= count_) {
        return nullptr;   // 越界 / 未初始化 → 调用方降级 no-op
    }
    accums_[globalSlot].level.store(static_cast<uint32_t>(level), std::memory_order_relaxed);
    return &accums_[globalSlot];
}

GlobalAccum* GlobalPerfPointFactory::Array() {
    return accums_;
}

uint32_t GlobalPerfPointFactory::Count() const {
    return count_;
}

}
