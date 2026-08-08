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

#include <gtest/gtest.h>
#include "spdiag/signal_watcher.h"
#include "spdiag/logger.h"

#include <csignal>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <atomic>

TEST(SignalWatcherTest, InitialStateNotInterrupted) {
    SpDiag::SignalWatcher watcher;
    EXPECT_FALSE(watcher.IsInterrupted());
}

TEST(SignalWatcherTest, CallbackInvokedOnSignal) {
    SpDiag::SignalWatcher watcher;
    std::atomic<bool> callback_called{false};

    watcher.BlockAndStart({SIGUSR1}, [&callback_called]() {
        callback_called.store(true);
    });

    // 向自身发送 SIGUSR1
    kill(getpid(), SIGUSR1);

    // 等待信号线程处理（最多 500ms）
    for (int i = 0; i < 50; ++i) {
        if (callback_called.load()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(callback_called.load());
    EXPECT_TRUE(watcher.IsInterrupted());

    watcher.Stop();
}

TEST(SignalWatcherTest, StopWithoutStartIsSafe) {
    SpDiag::SignalWatcher watcher;
    watcher.Stop();  // 未调用 BlockAndStart，Stop 应安全返回
    EXPECT_FALSE(watcher.IsInterrupted());
}

TEST(SignalWatcherTest, MultipleStopCallsAreSafe) {
    SpDiag::SignalWatcher watcher;
    std::atomic<bool> callback_called{false};

    watcher.BlockAndStart({SIGUSR1}, [&callback_called]() {
        callback_called.store(true);
    });

    kill(getpid(), SIGUSR1);

    for (int i = 0; i < 50; ++i) {
        if (callback_called.load()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    watcher.Stop();
    watcher.Stop();  // 重复 Stop 应安全
    EXPECT_TRUE(watcher.IsInterrupted());
}

TEST(SignalWatcherTest, FlushCallbackFlushesLogger) {
    SpDiag::SignalWatcher watcher;
    std::atomic<bool> flush_called{false};

    watcher.BlockAndStart({SIGUSR1}, [&flush_called]() {
        SpDiag::LoggerCore::GetInstance().FlushAll();
        flush_called.store(true);
    });

    kill(getpid(), SIGUSR1);

    for (int i = 0; i < 50; ++i) {
        if (watcher.IsInterrupted()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(watcher.IsInterrupted());
    EXPECT_TRUE(flush_called.load()) << "Flush callback was not invoked";

    watcher.Stop();
}
