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

#include <csignal>
#include <atomic>
#include <thread>
#include <functional>
#include <initializer_list>
#include <pthread.h>

namespace SpDiag {

/**
 * 信号观察器：通过 sigwait + 专用线程安全处理信号。
 *
 * 传统 signal()/sigaction() handler 处于异步信号上下文，受 async-signal-safe
 * 限制，不能使用 mutex、ofstream 等。SignalWatcher 改用专用线程 sigwait()
 * 同步等待信号，回调在正常线程上下文执行，可安全使用上述设施。
 *
 * 使用方式：
 *   SignalWatcher watcher;
 *   watcher.BlockAndStart({SIGINT, SIGTERM}, []() {
 *       LoggerCore::GetInstance().FlushAll();
 *   });
 *   // ... 业务逻辑，轮询 watcher.IsInterrupted() ...
 *   watcher.Stop();
 *
 * 注意：BlockAndStart() 必须在创建其他线程之前调用，确保信号屏蔽掩码
 * 被所有后续线程继承，信号不会异步递送。
 *
 * 内部机制：使用 SIGRTMIN 作为内部中断信号，Stop() 时通过 pthread_kill
 * 唤醒阻塞在 sigwait() 上的信号线程，确保安全退出（join 而非 detach）。
 */
class SignalWatcher {
public:
    using FlushCallback = std::function<void()>;

    SignalWatcher() = default;
    ~SignalWatcher();

    SignalWatcher(const SignalWatcher&) = delete;
    SignalWatcher& operator=(const SignalWatcher&) = delete;

    /**
     * 屏蔽指定信号并启动信号监听线程。
     *
     * 在当前线程屏蔽目标信号及内部中断信号 SIGRTMIN（后续创建的线程继承此掩码），
     * 然后启动专用线程通过 sigwait() 同步等待。
     *
     * @param signals  要捕获的信号列表（如 {SIGINT, SIGTERM}）
     * @param on_signal 收到信号时的回调（如 FlushAll）
     */
    void BlockAndStart(std::initializer_list<int> signals, FlushCallback on_signal);

    /**
     * 停止信号监听线程。
     * 通过 pthread_kill 发送 SIGRTMIN 唤醒阻塞在 sigwait() 上的线程，
     * 然后 join() 等待线程退出，确保无生命周期风险。
     */
    void Stop();

    /**
     * 检查是否收到终止信号（供主循环轮询）。
     */
    bool IsInterrupted() const { return interrupted_.load(); }

private:
    std::atomic<bool> interrupted_{false};
    std::atomic<bool> running_{false};
    std::thread signal_thread_;
    pthread_t signal_thread_handle_{};
    FlushCallback callback_;
    // BlockAndStart 前保存的调用线程原始信号掩码，Stop() 时恢复，
    // 避免进程在 Stop 后仍被屏蔽目标信号（如 SIGINT/SIGTERM 无法触发默认行为）。
    sigset_t old_mask_{};
    bool mask_saved_ = false;  // 是否成功保存了原始掩码
};

} // namespace SpDiag
