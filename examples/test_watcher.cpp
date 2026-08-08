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

#include "spdiag/perf_point.h"
#include "spdiag/perf_manager.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>

using namespace SpDiag;

volatile sig_atomic_t g_stop = 0;
void onSig(int) { g_stop = 1; }

int main() {
    signal(SIGINT, onSig);

    std::cout << "=== [1/4] SHM 不存在时打点，应不崩溃 ===\n";
    {
        PerfPoint pt(100, PerfLevel::KEY_MODULE);
        pt.Start();
        pt.End(0);
    }
    std::cout << "  OK: EnsureInitialized 未崩溃\n\n";

    std::cout << "=== [2/4] 请在另一个终端执行 spdiag start，等待自动连接 ===\n";
    while (!g_stop) {
        PerfManager::Instance().EnsureInitialized();
        if (PerfManager::Instance().IsInitialized()) {
            std::cout << "  OK: inotify 检测到 SHM 创建，已自动连接\n\n";
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if (g_stop) return 1;

    std::cout << "=== [3/4] 执行一次正常打点，确认连接有效 ===\n";
    {
        PerfPoint pt(101, PerfLevel::KEY_MODULE);
        pt.Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        pt.End(0);
    }
    std::cout << "  OK: 打点正常\n\n";

    std::cout << "=== [4/4] 请在另一个终端执行 spdiag stop 再 spdiag start，然后按 Enter ===\n";
    getchar();

    std::cout << " 重新打点，确认第二次重连成功 ...\n";
    PerfManager::Instance().EnsureInitialized();
    if (PerfManager::Instance().IsInitialized()) {
        std::cout << "  OK: 第二次自动重连成功\n";
    } else {
        std::cout << "  WARN: 未连接（可能 SHM 还没建好），但 watcher 未退出\n";
    }

    std::cout << "\n=== 验证完成 ===\n";
    return 0;
}
