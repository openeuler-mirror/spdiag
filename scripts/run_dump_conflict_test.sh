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

[ ! -x "$0" ] && chmod +x "$0" && exec "$0" "$@"
#
# 多程序并发转储冲突测试
#
# 用法: ./run_dump_conflict_test.sh <programs> <threads> <duration> [dump_interval_ms]
#
# 示例:
#   ./run_dump_conflict_test.sh 2 4 30      # 2 个程序，每个 4 线程，30 秒
#   ./run_dump_conflict_test.sh 8 4 60 500  # 8 个程序，每个 4 线程，60 秒，500ms 转储间隔
#

set -e

PROGRAMS=${1:-8}
THREADS=${2:-4}
DURATION=${3:-30}
DUMP_INTERVAL=${4:-500}
READ_INTERVAL=${5:-500}
SHM_NAME="/spdiag_shm_default"
BUILD_DIR="${BUILD_DIR:-build}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CONFIG_FILE="$PROJECT_DIR/config/spdiag.conf"

# 清理函数
cleanup() {
    echo ""
    echo "=== Cleaning up ==="
    # 杀掉所有 worker
    for pid in ${WORKER_PIDS[*]}; do
        kill "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
    # 停止 spdiag
    spdiag stop 2>/dev/null || true
    rm -f "$CONFIG_FILE"
    echo "Done."
}
trap cleanup EXIT

echo "=== Dump Conflict Test ==="
echo "  Programs:      $PROGRAMS"
echo "  Threads/proc:  $THREADS"
echo "  Duration:      ${DURATION}s"
echo "  Dump interval: ${DUMP_INTERVAL}ms"
echo "  Read interval: ${READ_INTERVAL}ms"
echo ""

# 检查可执行文件
WORKER="${BUILD_DIR}/tests/test_dump_conflict_worker"
READER="${BUILD_DIR}/tests/test_dump_conflict_reader"

if [ ! -x "$WORKER" ]; then
    echo "ERROR: $WORKER not found. Build first: cmake --build $BUILD_DIR --target test_dump_conflict_worker"
    exit 1
fi
if [ ! -x "$READER" ]; then
    echo "ERROR: $READER not found. Build first: cmake --build $BUILD_DIR --target test_dump_conflict_reader"
    exit 1
fi

# 基于 spdiag.conf.example 生成配置文件
mkdir -p "$PROJECT_DIR/config"
cp "$PROJECT_DIR/config/spdiag.conf.example" "$CONFIG_FILE"

# 追加测试参数（覆盖 example 中的默认值）
cat >> "$CONFIG_FILE" <<EOF

# --- test override ---
dump_interval_ms=$DUMP_INTERVAL
maxPrograms=$PROGRAMS
maxSlots=1024
EOF

# 从项目根目录启动 spdiag（确保能读到 config/spdiag.conf）
echo "--- Starting spdiag ---"
spdiag stop 2>/dev/null || true
(cd "$PROJECT_DIR" && spdiag start)
sleep 1

# 验证共享内存已创建
if ! spdiag status 2>/dev/null; then
    echo "ERROR: spdiag start failed"
    exit 1
fi
echo ""

# 启动 worker 进程
echo "--- Launching $PROGRAMS workers ---"
WORKER_PIDS=()
for i in $(seq 1 "$PROGRAMS"); do
    NAME=$(printf "prog_%03d" "$i")
    "$WORKER" -n "$NAME" -t "$THREADS" -d "$DURATION" -di "$DUMP_INTERVAL" &
    WORKER_PIDS+=($!)
    echo "  Started $NAME (PID $!)"
done
echo ""

# 等待 worker 注册完成
echo "--- Waiting for workers to register ---"
sleep 3

# 启动 reader
echo "--- Starting reader (${DURATION}s) ---"
echo ""
"$READER" -d "$DURATION" -i "$READ_INTERVAL"
READER_EXIT=$?
echo ""

# 等待 worker 结束
echo "--- Waiting for workers to finish ---"
for pid in ${WORKER_PIDS[*]}; do
    wait "$pid" 2>/dev/null || true
done

echo ""
echo "=== Test Complete ==="
if [ $READER_EXIT -eq 0 ]; then
    echo "RESULT: PASS (no failed reads)"
else
    echo "RESULT: FAIL (some reads failed)"
fi

exit $READER_EXIT
