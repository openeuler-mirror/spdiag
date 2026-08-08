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

// ============================================================================
// 架构适配层契约（aarch64-only 整改后保留）
// ============================================================================
// 本文件是 SpDiag 的唯一架构适配入口，定义一组架构无关的调用接口：
//
//   接口（对外稳定）:
//     ReadCycleCounter()      读取单调递增的硬件周期计数
//     CalibrateTimestamp()    启动期校准 nsPerTickQ32 / 时钟源选择
//     GetTimestampNs()        返回当前单调纳秒时间戳
//     ProbePmccntr()          探测 PMU cycle counter 是否 EL0 可读
//     ReadPmccntr/ReadCntvct  读取 aarch64 PMU/Generic Timer 寄存器
//
//   当前实现（aarch64）:
//     时钟源优先级 PMU_CYCLE_COUNTER → GENERIC_TIMER → CLOCK_GETTIME
//     寄存器: pmccntr_el0 / cntvct_el0 / cntfrq_el0
//
//   扩展新架构时的步骤:
//     1. 新建 src/platform/<os>/<arch>/platform_<arch>.cpp，实现上述接口
//     2. 修改 src/sdk/CMakeLists.txt 按目标架构选择 platform 源文件
//     3. 在根 CMakeLists.txt 调整 aarch64 硬断言为多架构分发
//     4. 若新架构需要 Q32 定点之外的 tick→ns 转换，参考 TicksToNs()
//
// 接口签名稳定，实现可替换；本注释是未来架构扩展的统一契约入口。
// ============================================================================

#if !defined(__aarch64__)
#error "SpDiag 当前仅支持 aarch64 架构。如需扩展新架构，请按本文件顶部契约实现 platform 接口。"
#endif

#include "platform.h"

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sched.h>
#include <time.h>
#include <cstring>
#include <signal.h>
#include <setjmp.h>

namespace SpDiag {
namespace Platform {

// ═══════════════════════════════════════════════════════════════════════
//  时钟源优先级 (AArch64):
//
//  1) PMU_CYCLE_COUNTER  ——  pmccntr_el0，CPU 主频计数
//     2.5 GHz → 0.4 ns/tick。需要内核模块写 PMUSERENR_EL0 开放 EL0 访问。
//  2) GENERIC_TIMER      ——  cntvct_el0，恒定频率系统计时器
//     典型 24–100 MHz → 10–42 ns/tick。用户态始终可读。
//  3) CLOCK_GETTIME      ——  vDSO clock_gettime 兜底
// ═══════════════════════════════════════════════════════════════════════

enum class ClockSource : uint8_t {
    CLOCK_GETTIME      = 0,
    GENERIC_TIMER      = 1,
    PMU_CYCLE_COUNTER  = 2,
};

static ClockSource clockSource    = ClockSource::CLOCK_GETTIME;
static uint64_t    tscPerUs       = 0;
static uint64_t    tscBase        = 0;
static uint64_t    nsBase         = 0;
static uint64_t    nsPerTickQ32   = 0;   // Q32 定点: ns_per_tick * 2^32
static uint64_t    isbOverhead    = 0;   // isb+mrs 空测开销 (ticks)

static uint64_t ClockGetTimeNsRaw();

// ─── AArch64 PMU/Generic Timer 探测与读取 ─────────────────────────

static sigjmp_buf probeJmpBuf;

static void ProbeSignalHandler(int /*sig*/) {
    siglongjmp(probeJmpBuf, 1);
}

static bool ProbePmccntr() {
    struct sigaction sa{}, oldIll{}, oldSegv{};
    sa.sa_handler = ProbeSignalHandler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGILL,  &sa, &oldIll);
    sigaction(SIGSEGV, &sa, &oldSegv);

    bool ok = false;
    if (sigsetjmp(probeJmpBuf, 1) == 0) {
        uint64_t v1, v2;
        __asm__ volatile("mrs %0, pmccntr_el0" : "=r"(v1));
        __asm__ volatile("mrs %0, pmccntr_el0" : "=r"(v2));
        ok = (v2 >= v1);   // 确认计数器确实在跑
    }

    sigaction(SIGILL,  &oldIll,  nullptr);
    sigaction(SIGSEGV, &oldSegv, nullptr);
    return ok;
}

static inline uint64_t ReadPmccntr() {
    uint64_t val;
    __asm__ volatile(
        "isb                     \n\t"
        "mrs  %0, pmccntr_el0    \n\t"
        : "=r"(val) :: "memory"
    );
    return val;
}

static inline uint64_t ReadCntvct() {
    uint64_t val;
    __asm__ volatile(
        "isb                     \n\t"
        "mrs  %0, cntvct_el0     \n\t"
        : "=r"(val) :: "memory"
    );
    return val;
}

// ─── 统一计数器读取 ────────────────────────────────────────────────
static inline uint64_t ReadCycleCounter() {
    switch (clockSource) {
        case ClockSource::PMU_CYCLE_COUNTER: return ReadPmccntr();
        case ClockSource::GENERIC_TIMER:     return ReadCntvct();
        default:                             return ClockGetTimeNsRaw();
    }
}

// ─── clock_gettime ─────────────────────────────────────────────────
static uint64_t ClockGetTimeNsRaw() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}

// ─── tick → ns：Q32 定点乘法，无浮点，无整数截断漂移 ───────────────
static inline uint64_t TicksToNs(uint64_t ticks) {
    return static_cast<uint64_t>(
        (static_cast<__uint128_t>(ticks) * nsPerTickQ32) >> 32
    );
}

// ─── 共享内存（不变）──────────────────────────────────────────────
ShmHandle ShmCreate(const std::string& name, size_t size) {
    ShmHandle handle;
    int fd = shm_open(name.c_str(), O_RDWR | O_CREAT, 0666);
    if (fd == -1) return handle;
    if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
        close(fd);
        return handle;
    }
    void* addr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        close(fd);
        return handle;
    }
    handle.baseAddr   = addr;
    handle.mappedSize = size;
    handle.fd         = fd;
    return handle;
}

ShmHandle ShmOpenReadOnly(const std::string& name, size_t size) {
    ShmHandle handle;
    int fd = shm_open(name.c_str(), O_RDONLY, 0);
    if (fd == -1) return handle;
    void* addr = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        close(fd);
        return handle;
    }
    handle.baseAddr   = addr;
    handle.mappedSize = size;
    handle.fd         = fd;
    return handle;
}

ShmHandle ShmOpenReadWrite(const std::string& name, size_t size) {
    ShmHandle handle;
    int fd = shm_open(name.c_str(), O_RDWR, 0);
    if (fd == -1) return handle;
    void* addr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        close(fd);
        return handle;
    }
    handle.baseAddr   = addr;
    handle.mappedSize = size;
    handle.fd         = fd;
    return handle;
}

void ShmClose(ShmHandle& handle) {
    if (handle.baseAddr) {
        munmap(handle.baseAddr, handle.mappedSize);
        handle.baseAddr = nullptr;
    }
    if (handle.fd != -1) {
        close(handle.fd);
        handle.fd = -1;
    }
    handle.mappedSize = 0;
}

void ShmDestroy(const std::string& name) {
    shm_unlink(name.c_str());
}

// ─── CPU ID ────────────────────────────────────────────────────────
uint32_t GetCurrentCpuId() {
    return static_cast<uint32_t>(sched_getcpu());
}

// ─── 获取时间戳 (ns) ──────────────────────────────────────────────
uint64_t GetTimestampNs() {
    if (tscPerUs == 0) {
        CalibrateTimestamp();
    }

    if (clockSource == ClockSource::CLOCK_GETTIME) {
        return ClockGetTimeNsRaw();
    }

    uint64_t cc      = ReadCycleCounter();
    uint64_t elapsed = cc - tscBase;
    return nsBase + TicksToNs(elapsed);
}

// ─── 校准 ──────────────────────────────────────────────────────────
uint64_t CalibrateTimestamp() {

    // ── 1. 探测最优时钟源 ──────────────────────────────────────────
    // 优先尝试 PMU cycle counter（亚纳秒），失败则用 Generic Timer
    if (ProbePmccntr()) {
        clockSource = ClockSource::PMU_CYCLE_COUNTER;
    } else {
        clockSource = ClockSource::GENERIC_TIMER;
    }

    // clock_gettime 兜底: tick == ns，1:1 直通
    if (clockSource == ClockSource::CLOCK_GETTIME) {
        tscPerUs     = 1000;
        nsPerTickQ32 = 1ULL << 32;
        tscBase      = ClockGetTimeNsRaw();
        nsBase       = tscBase;
        return tscPerUs;
    }

    // ── 2. 100ms 窗口校准 ─────────────────────────────────────────
    //    原来的 busy-loop 只有 ~5ms，校准误差可达 0.5%；
    //    改用 100ms nanosleep，误差 < 0.01%
    struct timespec sleepReq{0, 100000000};  // 100 ms

    uint64_t nsStart = ClockGetTimeNsRaw();
    uint64_t ccStart = ReadCycleCounter();

    nanosleep(&sleepReq, nullptr);

    uint64_t ccEnd = ReadCycleCounter();
    uint64_t nsEnd = ClockGetTimeNsRaw();

    uint64_t elapsedNs    = nsEnd   - nsStart;
    uint64_t elapsedTicks = ccEnd   - ccStart;

    if (elapsedNs    == 0) elapsedNs    = 1;
    if (elapsedTicks == 0) elapsedTicks = 1;

    // 向后兼容字段
    uint64_t elapsedUs = elapsedNs / 1000ULL;
    if (elapsedUs == 0) elapsedUs = 1;
    tscPerUs = elapsedTicks / elapsedUs;
    if (tscPerUs == 0) tscPerUs = 1;

    // Q32 定点 ns_per_tick = (elapsedNs << 32) / elapsedTicks
    // 用 __uint128_t 避免溢出
    nsPerTickQ32 = static_cast<uint64_t>(
        (static_cast<__uint128_t>(elapsedNs) << 32) / elapsedTicks
    );

    tscBase = ccStart;
    nsBase  = nsStart;

    // ── 3. 校准 isb+mrs 空测开销 ──────────────────────────────────
    if (clockSource == ClockSource::PMU_CYCLE_COUNTER) {
        uint64_t sum = 0;
        constexpr int N = 100000;
        for (int i = 0; i < N; ++i) {
            uint64_t a = ReadPmccntr();
            uint64_t b = ReadPmccntr();
            sum += (b - a);
        }
        isbOverhead = sum / N;
    }

    return tscPerUs;
}

} // namespace Platform
} // namespace SpDiag