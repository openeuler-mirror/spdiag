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
    callback_ = std::move(on_signal);

    sigset_t mask;
    sigemptyset(&mask);
    for (int sig : signals) {
        sigaddset(&mask, sig);
    }
    sigaddset(&mask, SIGRTMIN);
    pthread_sigmask(SIG_BLOCK, &mask, nullptr);

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
    pthread_kill(signal_thread_handle_, SIGRTMIN);
    if (signal_thread_.joinable()) {
        signal_thread_.join();
    }
}

} // namespace SpDiag
