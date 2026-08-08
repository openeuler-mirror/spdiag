#!/bin/bash
# Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
# SpDiag is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#         http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.

#
# ProbeDone 2.0 / SHM Watcher 全自动验证 + 代码级状态追踪 + .md 复盘
#
# 运行方式：
#   bash build.sh -s on
#   chmod +x examples/test_watcher_verify.sh
#   ./examples/test_watcher_verify.sh
#

set -o pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
PASS=0; FAIL=0

pass() { echo -e "${GREEN}[PASS]${NC} $*"; ((PASS++)); }
fail() { echo -e "${RED}[FAIL]${NC} $*"; ((FAIL++)); }
info() { echo -e "${YELLOW}[INFO]${NC} $*"; }

WATCHER_BIN="${WATCHER_BIN:-./build/examples/test_watcher_auto}"
SPDIAG_BIN="${SPDIAG_BIN:-spdiag}"
REPORT_FILE="watcher_verify_$(date +%Y%m%d_%H%M%S).md"

echo "========================================="
echo " ProbeDone 2.0 / SHM Watcher 状态追踪"
echo "========================================="
echo ""

[ -x "$WATCHER_BIN" ] || { fail "未找到 $WATCHER_BIN"; exit 1; }
command -v "$SPDIAG_BIN" &>/dev/null || { fail "spdiag 未安装"; exit 1; }
info "报告文件: $REPORT_FILE"
pass "预检通过"

# 清理
$SPDIAG_BIN stop 2>/dev/null || true
sleep 1

# ---- 启动测试程序（后台），输出同时 tee 到终端和临时文件 ----
OUTFILE=$(mktemp)
echo ""
info "启动 test_watcher_auto ..."
$WATCHER_BIN > "$OUTFILE" 2>&1 &
WATCHER_PID=$!
trap "kill $WATCHER_PID 2>/dev/null; rm -f $OUTFILE" EXIT

wait_for() {
    local pattern="$1"
    local timeout="${2:-30}"
    local waited=0
    while [ $waited -lt $timeout ]; do
        if grep -q "$pattern" "$OUTFILE" 2>/dev/null; then return 0; fi
        sleep 0.5
        ((waited++))
    done
    return 1
}

echo_md_header() {
    cat > "$REPORT_FILE" << 'MDHEAD'
# ProbeDone 2.0 / SHM Watcher 验证报告

**日期**：MDHEAD
    echo -n "$(date)" >> "$REPORT_FILE"
    cat >> "$REPORT_FILE" << 'MDHEAD'

**环境**：openEuler 24.03 aarch64, GCC 12.3.1  
**分支**：`feat/shm-probe-watcher`  
**被测组件**：`PerfManager::EnsureInitialized()` / `StartShmWatcher()` / `Shutdown()`

---

## 执行日志

```
MDHEAD
}

# ---- Phase 1 ----
echo ""
echo ">>> Phase 1: SHM 不存在时 probeDone_ 失败标记"
if ! wait_for "\[OK:Phase1\]" 20; then
    fail "Phase 1 — 超时"
    cat "$OUTFILE"
    exit 1
fi
# 展示 Phase 1 输出
grep -A200 "Phase 1" "$OUTFILE" | while IFS= read -r line; do
    if echo "$line" | grep -qE "Phase 2|\[OK:Phase2\]"; then break; fi
    echo -e "  ${CYAN}$line${NC}"
done
pass "Phase 1 — probeDone_ 失败标记 + fast guard 验证通过"

# ---- Phase 2 ----
echo ""
echo ">>> Phase 2: spdiag start → watcher IN_CREATE → 自动连接"
sleep 1
$SPDIAG_BIN start --perflog 2>&1
info "SHM 已创建，等待 inotify 检测 ..."

if ! wait_for "\[OK:Phase2\]" 70; then
    fail "Phase 2 — 连接超时"
    cat "$OUTFILE"
    exit 1
fi
pass "Phase 2 — watcher IN_CREATE → 自动连接成功"

# ---- Phase 3 ----
echo ""
echo ">>> Phase 3: 正常打点数据通路"
if ! wait_for "\[OK:Phase3\]" 20; then
    fail "Phase 3 — 超时"
    cat "$OUTFILE"
    exit 1
fi
pass "Phase 3 — fast guard 命中，数据通路正常"

# ---- Phase 4 ----
echo ""
echo ">>> Phase 4: spdiag stop → start → watcher 自动重连"
sleep 1
$SPDIAG_BIN stop 2>&1
info "SHM 已停止 (watcher 应检测 IN_DELETE)"
sleep 2
$SPDIAG_BIN start --perflog 2>&1
info "SHM 已重建 (watcher 应检测 IN_CREATE)"

if ! wait_for "\[OK:Phase4\]" 70; then
    fail "Phase 4 — 重连超时"
    cat "$OUTFILE"
    exit 1
fi
pass "Phase 4 — watcher DELETE/CREATE → 自动重连成功"

# ---- 等待完成 ----
wait $WATCHER_PID 2>/dev/null || true
$SPDIAG_BIN stop 2>/dev/null || true
trap - EXIT

# ---- 生成 .md 复盘文件 ----
echo ""
info "生成复盘报告: $REPORT_FILE"
echo_md_header

# 将完整 C++ 输出嵌入 .md
cat "$OUTFILE" >> "$REPORT_FILE"

cat >> "$REPORT_FILE" << 'MDEND'
```

---

## 验证结论

| # | 验证项 | 状态 | 关键代码路径 |
|---|--------|------|-------------|
| 1 | `probeDone_` 失败标记 | PASS | perf_manager.cpp:132 SHM 不存在→probeDone_=true |
| 2 | fast guard 分流 | PASS | perf_manager.cpp:112 probeDone_.load(acquire) 一步跳过 |
| 3 | `StartShmWatcher()` inotify 检测 | PASS | perf_manager.cpp:303-314 IN_CREATE/IN_DELETE 事件处理 |
| 4 | `ConnectToExistingShm()` 重连 | PASS | perf_manager.cpp:200-237 重建连接+PerfTimer |
| 5 | 数据通路 | PASS | perf_point.cpp:137 GetShard().stats[slotIndex_] 写入 |

## 涉及的关键源文件

| 文件 | 行号 | 功能 |
|------|------|------|
| `include/spdiag/perf_manager.h` | 65-68 | probeDone_/watcherStarted_/shmWatcherStop_/shmWatcher_ 声明 |
| `src/sdk/perf_manager.cpp` | 111-194 | EnsureInitialized()  fast guard + 慢路径探测 |
| `src/sdk/perf_manager.cpp` | 269-322 | StartShmWatcher() inotify 后台线程 |
| `src/sdk/perf_manager.cpp` | 200-237 | ConnectToExistingShm() 重建连接 |
| `src/sdk/perf_manager.cpp` | 239-267 | Shutdown() watcher→timer→shmWriter 销毁顺序 |
| `include/spdiag/shm_layout.h` | 179 | ShmHeader::generation 字段 |

---

*报告自动生成于 $(date)*
MDEND

# ---- 汇总 ----
echo ""
echo "========================================="
echo " 验证结果: PASS=$PASS  FAIL=$FAIL"
echo " 复盘报告: $REPORT_FILE"
echo "========================================="
rm -f "$OUTFILE"

if [ "$FAIL" -eq 0 ]; then
    echo -e "${GREEN}全部通过!${NC}"
    exit 0
else
    echo -e "${RED}有 $FAIL 项失败${NC}"
    exit 1
fi
