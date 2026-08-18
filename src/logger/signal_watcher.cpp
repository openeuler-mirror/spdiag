/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
 * SpDiag is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *         http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * See the Mulan PSL v2 for more details.
 */

#include "spdiag/signal_watcher.h"

#include <pthread.h>
#include <utility>

namespace SpDiag {

SignalWatcher::~SignalWatcher() {
    Stop();
}

void SignalWatcher::BlockAndStart(std::initializer_list<int> signals,
                                   FlushCallback on_signal) {
    // 支持重复调用（重新武装）：若上一次的监听线程仍在运行，先安全停止并 join，
    // 否则对仍 joinable 的 std::thread 对象直接赋值会触发 std::terminate。
    // 首次调用时 running_ 为 false，Stop() 立即返回，无额外开销。
    Stop();

    callback_ = std::move(on_signal);
    interrupted_.store(false);  // 支持重复 BlockAndStart（重新武装）

    sigset_t mask;
    sigemptyset(&mask);
    for (int sig : signals) {
        sigaddset(&mask, sig);
    }
    sigaddset(&mask, SIGRTMIN);
    // 保存调用线程的原始掩码，供 Stop() 恢复；保存失败时仅不恢复，不影响主流程。
    if (pthread_sigmask(SIG_BLOCK, &mask, &old_mask_) == 0) {
        mask_saved_ = true;
    }

    running_.store(true);
    signal_thread_ = std::thread([this, mask]() {
        while (running_.load()) {
            int sig = 0;
            int ret = sigwait(&mask, &sig);
            if (ret != 0) {
                continue;
            }

            if (sig == SIGRTMIN) {
                continue;
            }

            if (callback_) {
                callback_();
            }
            interrupted_.store(true);
            break;
        }
    });
    signal_thread_handle_ = signal_thread_.native_handle();
}

void SignalWatcher::Stop() {
    if (!running_.exchange(false)) {
        return;
    }
    // 若线程因收到真实信号已自行退出（interrupted_ 为 true），无需再向其
    // 发送唤醒信号（对已退出的线程执行 pthread_kill 属无效操作）。
    if (!interrupted_.load()) {
        pthread_kill(signal_thread_handle_, SIGRTMIN);
    }
    if (signal_thread_.joinable()) {
        signal_thread_.join();
    }
    // 恢复调用线程在 BlockAndStart 前的信号掩码，避免 Stop 后进程仍被屏蔽
    // 目标信号。注意：BlockAndStart 期间创建的线程继承了屏蔽掩码，本接口
    // 仅恢复调用线程自身，后续线程的信号行为由使用者自行保证。
    if (mask_saved_) {
        pthread_sigmask(SIG_SETMASK, &old_mask_, nullptr);
        mask_saved_ = false;
    }
}

} // namespace SpDiag
