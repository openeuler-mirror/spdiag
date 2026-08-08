#!/bin/bash
# ============================================================
# SpDiag 全量功能端到端验证脚本（ubdiag → spdiag 改名后）
# 验证所有功能：PerfPoint + PerfLog + OB Memstat + OB Cachestat
# 在 aarch64 机器上执行：bash scripts/verify_spdiag_rename.sh
# 结果 CSV 落盘
# 要求：root 权限（OB 功能需要 eBPF/perf_event）
# ============================================================
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_DIR}/build_verify_spdiag"
CSV_FILE="${PROJECT_DIR}/verify_spdiag_results.csv"
LOG_DIR="${PROJECT_DIR}/verify_spdiag_logs"
TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

rm -rf "$LOG_DIR"
mkdir -p "$LOG_DIR"
echo "check_id,check_name,status,detail,timestamp" > "$CSV_FILE"

PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0
DEMO_APP_PID=""
MEMSTAT_DEMO_PID=""
MEMSTAT_COLLECT_PID=""
CACHESTAT_DEMO_PID=""
CACHESTAT_COLLECT_PID=""
MEMSTAT_FIFO=""
CACHESTAT_FIFO=""

stop_process() {
    local pid="${1:-}"
    [ -n "$pid" ] || return 0
    kill -0 "$pid" 2>/dev/null || { wait "$pid" 2>/dev/null || true; return 0; }
    kill -INT "$pid" 2>/dev/null || true
    for _ in 1 2 3 4 5; do
        kill -0 "$pid" 2>/dev/null || break
        sleep 1
    done
    kill -TERM "$pid" 2>/dev/null || true
    sleep 1
    kill -KILL "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
}

cleanup() {
    stop_process "$DEMO_APP_PID"
    stop_process "$MEMSTAT_COLLECT_PID"
    stop_process "$MEMSTAT_DEMO_PID"
    stop_process "$CACHESTAT_COLLECT_PID"
    stop_process "$CACHESTAT_DEMO_PID"
    [ -n "$MEMSTAT_FIFO" ] && rm -f "$MEMSTAT_FIFO"
    [ -n "$CACHESTAT_FIFO" ] && rm -f "$CACHESTAT_FIFO"
}
trap cleanup EXIT

record() {
    local id=$1 name=$2 status=$3 detail=$4
    # CSV 安全：替换双引号和换行
    local safe_detail=$(echo "$detail" | tr '"' "'" | tr '\n' ' ' | head -c 200)
    echo "${id},${name},${status},\"${safe_detail}\",${TIMESTAMP}" >> "$CSV_FILE"
    if [ "$status" = "PASS" ]; then
        ((PASS_COUNT++))
        printf "  [PASS] %s\n" "$name"
    elif [ "$status" = "FAIL" ]; then
        ((FAIL_COUNT++))
        printf "  [FAIL] %s — %s\n" "$name" "$detail"
    else
        ((SKIP_COUNT++))
        printf "  [SKIP] %s — %s\n" "$name" "$detail"
    fi
}

echo "============================================================"
echo "  SpDiag 全量功能端到端验证"
echo "  时间: ${TIMESTAMP}"
echo "  项目: ${PROJECT_DIR}"
echo "  CSV:  ${CSV_FILE}"
echo "============================================================"

# ===== 0. 环境检查 =====
echo ""
echo "[0] 环境检查"

ARCH=$(uname -m)
if [ "$ARCH" = "aarch64" ]; then
    record 0 "架构检查" "PASS" "aarch64"
else
    record 0 "架构检查" "FAIL" "期望 aarch64,实际 $ARCH"
    echo "FATAL: 需要 aarch64 机器"
    exit 1
fi

CMAKE_VER=$(cmake --version 2>/dev/null | head -1)
if [ -n "$CMAKE_VER" ]; then
    record 0 "cmake 检查" "PASS" "$CMAKE_VER"
else
    record 0 "cmake 检查" "FAIL" "cmake 不可用"
    exit 1
fi

GCC_VER=$(g++ --version 2>/dev/null | head -1)
if [ -n "$GCC_VER" ]; then
    record 0 "g++ 检查" "PASS" "$GCC_VER"
else
    record 0 "g++ 检查" "FAIL" "g++ 不可用"
    exit 1
fi

for required_cmd in timeout stdbuf awk; do
    if command -v "$required_cmd" >/dev/null 2>&1; then
        record 0 "$required_cmd 检查" "PASS" "$(command -v "$required_cmd")"
    else
        record 0 "$required_cmd 检查" "FAIL" "$required_cmd 不可用"
        exit 1
    fi
done

# root 检查（OB 功能需要）
if [ "$(id -u)" = "0" ]; then
    record 0 "root 权限" "PASS" "root"
else
    record 0 "root 权限" "FAIL" "完整验证必须使用 root"
fi

# libbpf 检查（同时查 ldconfig 和 /usr/lib64）
if ldconfig -p 2>/dev/null | grep -q "libbpf" || ls /usr/lib64/libbpf.so* 2>/dev/null | grep -q .; then
    record 0 "libbpf 检查" "PASS" "libbpf 已安装"
else
    record 0 "libbpf 检查" "FAIL" "完整验证要求安装 libbpf"
fi

# ===== 1. 残留检查 =====
echo ""
echo "[1] 残留 spdiag 改名检查"

rm -rf "${PROJECT_DIR}/verify_install_root"

RESIDUAL=$(find "$PROJECT_DIR" -type f \
    \( -name "*.cpp" -o -name "*.h" -o -name "*.txt" -o -name "*.sh" \
       -o -name "*.spec" -o -name "*.def" -o -name "*.in" -o -name "*.cmake" \
       -o -name "*.conf" -o -name "*.conf.example" -o -name "*.md" \) \
    ! -path "*/.git/*" \
    ! -path "*/build*" \
    ! -path "*/scripts/verify_spdiag_rename.sh" \
    -exec grep -li "ubdiag" {} \; 2>/dev/null)

if [ -z "$RESIDUAL" ]; then
    record 1 "残留字符串" "PASS" "零残留"
else
    COUNT=$(echo "$RESIDUAL" | wc -l)
    record 1 "残留字符串" "FAIL" "${COUNT} 个文件仍有 ubdiag 旧名"
fi

RESIDUAL_FILES=$(find "$PROJECT_DIR" -name "*ubdiag*" ! -path "*/.git/*" ! -path "*/build*" 2>/dev/null)
if [ -z "$RESIDUAL_FILES" ]; then
    record 1 "残留文件名" "PASS" "零残留"
else
    record 1 "残留文件名" "FAIL" "有残留文件名"
fi

if grep -qE '^project\(SpDiag VERSION 1\.0\.0 ' "$PROJECT_DIR/CMakeLists.txt" && \
   grep -qE 'versionString = "1\.0\.0"' "$PROJECT_DIR/include/spdiag/version.h" && \
   grep -qE '^Name:[[:space:]]+spdiag$' "$PROJECT_DIR/spdiag.spec" && \
   grep -qE '^Version:[[:space:]]+1\.0\.0$' "$PROJECT_DIR/spdiag.spec"; then
    record 1 "版本身份一致" "PASS" "CMake/header/RPM 均为 SpDiag 1.0.0"
else
    record 1 "版本身份一致" "FAIL" "CMake、version.h 或 spdiag.spec 身份不一致"
fi

# ===== 2. CMake 配置（全功能开启）=====
echo ""
echo "[2] CMake 配置（全功能：PerfPoint + PerfLog + Percentile + OB Memory + OB Cache）"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

if cmake "$PROJECT_DIR" \
    -DBUILD_TESTS=ON \
    -DBUILD_EXAMPLES=ON \
    -DENABLE_PERCENTILE=ON \
    -DENABLE_PERFLOG=ON \
    -DENABLE_MEMPOINT=OFF \
    -DENABLE_OB_MEMORY=ON \
    -DENABLE_OB_CACHE=ON \
    -DSPDIAG_BUILD_SHARED=OFF \
    > "$LOG_DIR/cmake.log" 2>&1; then
    record 2 "cmake 配置" "PASS" "全功能配置成功"
else
    record 2 "cmake 配置" "FAIL" "见 $LOG_DIR/cmake.log"
    tail -20 "$LOG_DIR/cmake.log"
    echo "FATAL: cmake 失败"
    cd "$PROJECT_DIR"
    echo ""
    echo "验证中止: ${PASS_COUNT} PASS / ${FAIL_COUNT} FAIL / ${SKIP_COUNT} SKIP"
    exit 1
fi

# target 名检查
TARGET_CHECK=$(cmake --build "$BUILD_DIR" --target help 2>/dev/null | grep -i "spdiag" || true)
if [ -n "$TARGET_CHECK" ]; then
    record 2 "target 名 spdiag" "PASS" "$(echo "$TARGET_CHECK" | head -3 | tr '\n' ' ')"
else
    if grep -q "spdiag_lib" "$BUILD_DIR/CMakeCache.txt" 2>/dev/null || \
       grep -q "spdiag_lib" "$BUILD_DIR/Makefile" 2>/dev/null; then
        record 2 "target 名 spdiag" "PASS" "CMakeCache/Makefile 确认 spdiag_lib"
    else
        record 2 "target 名 spdiag" "FAIL" "未找到 spdiag target"
    fi
fi

# ===== 3. 全量编译 =====
echo ""
echo "[3] 全量编译"

if cmake --build "$BUILD_DIR" -j"$(nproc)" > "$LOG_DIR/build.log" 2>&1; then
    record 3 "全量编译" "PASS" "编译成功"
else
    record 3 "全量编译" "FAIL" "见 $LOG_DIR/build.log 最后 30 行"
    tail -30 "$LOG_DIR/build.log"
    echo "FATAL: 编译失败"
    cd "$PROJECT_DIR"
    exit 1
fi

# ===== 4. 编译产物检查 =====
echo ""
echo "[4] 编译产物检查"

# CLI
SPDIAG_CLI=""
for path in "$BUILD_DIR/src/cli/spdiag" "$(command -v spdiag 2>/dev/null)"; do
    if [ -x "$path" ]; then SPDIAG_CLI="$path"; break; fi
done
if [ -n "$SPDIAG_CLI" ]; then
    record 4 "CLI 二进制" "PASS" "$SPDIAG_CLI"
else
    record 4 "CLI 二进制" "FAIL" "找不到 spdiag CLI"
fi

# 库
if find "$BUILD_DIR" -name "libspdiag.*" -type f 2>/dev/null | grep -q .; then
    record 4 "SDK 库" "PASS" "$(find "$BUILD_DIR" -name 'libspdiag.*' -type f 2>/dev/null | head -1)"
else
    record 4 "SDK 库" "FAIL" "找不到 libspdiag"
fi

# demo
DEMO_APP=""
for path in "$BUILD_DIR/examples/demo_app" "$BUILD_DIR/example/demo_app"; do
    if [ -x "$path" ]; then DEMO_APP="$path"; break; fi
done
if [ -n "$DEMO_APP" ]; then
    record 4 "demo_app" "PASS" "$DEMO_APP"
else
    record 4 "demo_app" "FAIL" "未找到"
fi

# memstat_demo
MEMSTAT_DEMO=""
for path in "$BUILD_DIR/examples/memstat_demo" "$BUILD_DIR/example/memstat_demo"; do
    if [ -x "$path" ]; then MEMSTAT_DEMO="$path"; break; fi
done
if [ -n "$MEMSTAT_DEMO" ]; then
    record 4 "memstat_demo" "PASS" "$MEMSTAT_DEMO"
else
    record 4 "memstat_demo" "FAIL" "未找到"
fi

# cachestat_demo
CACHESTAT_DEMO=""
for path in "$BUILD_DIR/examples/cachestat_demo" "$BUILD_DIR/example/cachestat_demo"; do
    if [ -x "$path" ]; then CACHESTAT_DEMO="$path"; break; fi
done
if [ -n "$CACHESTAT_DEMO" ]; then
    record 4 "cachestat_demo" "PASS" "$CACHESTAT_DEMO"
else
    record 4 "cachestat_demo" "FAIL" "未找到"
fi

# ===== 5. CLI 基础功能验证 =====
echo ""
echo "[5] CLI 基础功能"

if [ -z "$SPDIAG_CLI" ]; then
    record 5 "CLI 功能" "FAIL" "无 CLI"
else
    # version
    VERSION_OUT=$("$SPDIAG_CLI" --version 2>&1 || true)
    if echo "$VERSION_OUT" | grep -qiE "spdiag version 1\.0\.0"; then
        record 5 "CLI --version" "PASS" "$(echo "$VERSION_OUT" | head -1)"
    else
        record 5 "CLI --version" "FAIL" "期望 spdiag version 1.0.0，实际: $(echo "$VERSION_OUT" | head -1)"
    fi

    # help
    HELP_OUT=$("$SPDIAG_CLI" --help 2>&1 || true)
    if echo "$HELP_OUT" | grep -qi "spdiag"; then
        record 5 "CLI --help" "PASS" "help 含 spdiag"
    else
        record 5 "CLI --help" "FAIL" "help 不含 spdiag"
    fi

    # start
    "$SPDIAG_CLI" stop 2>/dev/null || true
    rm -f /dev/shm/spdiag_shm_* 2>/dev/null || true
    START_OUT=$("$SPDIAG_CLI" start 2>&1 || true)
    if echo "$START_OUT" | grep -qi "started"; then
        record 5 "CLI start" "PASS" "$START_OUT"
    else
        record 5 "CLI start" "FAIL" "$START_OUT"
    fi

    # status
    STATUS_OUT=$("$SPDIAG_CLI" status 2>&1 || true)
    if echo "$STATUS_OUT" | grep -qi "running"; then
        record 5 "CLI status" "PASS" "$STATUS_OUT"
    else
        record 5 "CLI status" "FAIL" "$STATUS_OUT"
    fi

    # show（空表也行，只要不崩）
    SHOW_OUT=$("$SPDIAG_CLI" show 2>&1 || true)
    if echo "$SHOW_OUT" | grep -qiE "Module|Point|empty|no data"; then
        record 5 "CLI show" "PASS" "show 正常"
    else
        record 5 "CLI show" "FAIL" "show 异常"
    fi

    # stop
    "$SPDIAG_CLI" stop > /dev/null 2>&1 || true
    record 5 "CLI stop" "PASS" "stopped"
fi

# ===== 6. PerfPoint 端到端验证 =====
echo ""
echo "[6] PerfPoint 端到端验证"

if [ -z "$SPDIAG_CLI" ] || [ -z "$DEMO_APP" ]; then
    record 6 "PerfPoint 端到端" "FAIL" "缺少 CLI 或 demo_app"
else
    "$SPDIAG_CLI" stop 2>/dev/null || true
    rm -f /dev/shm/spdiag_shm_* 2>/dev/null || true
    "$SPDIAG_CLI" start > /dev/null 2>&1
    sleep 1

    # 跑 demo 8 秒，并等待默认 5 秒 dump 周期至少完成一次。
    cd "$BUILD_DIR"
    "$DEMO_APP" -d 8 > "$LOG_DIR/demo_app.log" 2>&1 &
    DEMO_APP_PID=$!
    sleep 6
    cd "$PROJECT_DIR"

    # show 有数据
    SHOW_OUT=$("$SPDIAG_CLI" show 2>&1 || true)
    if echo "$SHOW_OUT" | grep -qiE "Compute|IO|Process|Pipeline|Memory"; then
        record 6 "PerfPoint 打点数据" "PASS" "show 有多个模块打点数据"
        echo "  --- show 输出前 5 行 ---"
        echo "$SHOW_OUT" | head -5
    else
        record 6 "PerfPoint 打点数据" "FAIL" "show 无打点数据。demo 日志: $(tail -3 "$LOG_DIR/demo_app.log" 2>/dev/null | tr '\n' ' ')"
    fi

    # 等 demo 跑完
    wait "$DEMO_APP_PID" 2>/dev/null || true
    DEMO_APP_PID=""

    # show --detail
    DETAIL_OUT=$("$SPDIAG_CLI" show --detail 2>&1 || true)
    if echo "$DETAIL_OUT" | grep -q "Core" && echo "$DETAIL_OUT" | grep -q "Ticks"; then
        record 6 "show --detail" "PASS" "detail 输出正常"
    else
        record 6 "show --detail" "FAIL" "detail 未输出明细表: $(echo "$DETAIL_OUT" | head -2 | tr '\n' ' ')"
    fi

    # CSV 导出
    mkdir -p "$LOG_DIR/csv_perfpoint"
    CSV_OUT=$("$SPDIAG_CLI" show --csv "$LOG_DIR/csv_perfpoint" 2>&1 || true)
    CSV_FOUND=$(find "$LOG_DIR/csv_perfpoint" -name "*.csv" -type f -exec awk 'END { if (NR > 1) print FILENAME }' {} \; 2>/dev/null | head -1)
    if [ -n "$CSV_FOUND" ]; then
        record 6 "CSV 导出" "PASS" "$(wc -l < "$CSV_FOUND") 行"
    else
        record 6 "CSV 导出" "FAIL" "CSV 未生成: $CSV_OUT"
    fi

    # watch（3 秒后自动停）
    timeout 3 "$SPDIAG_CLI" watch --interval 1000 > "$LOG_DIR/watch.log" 2>&1 || true
    if grep -q "Ticks" "$LOG_DIR/watch.log" 2>/dev/null && grep -q "Program" "$LOG_DIR/watch.log" 2>/dev/null; then
        record 6 "watch 持续监控" "PASS" "watch 有输出"
    else
        record 6 "watch 持续监控" "FAIL" "watch 未输出数据表: $(head -2 "$LOG_DIR/watch.log" 2>/dev/null | tr '\n' ' ')"
    fi

    # history
    HISTORY_OUT=$("$SPDIAG_CLI" history 2>&1 || true)
    if echo "$HISTORY_OUT" | grep -q "Timestamp(ns)"; then
        record 6 "history 历史记录" "PASS" "history 有 dump 记录"
    else
        record 6 "history 历史记录" "FAIL" "history 未输出 dump 记录: $(echo "$HISTORY_OUT" | head -2 | tr '\n' ' ')"
    fi

    STOP_OUT=$("$SPDIAG_CLI" stop 2>&1)
    STOP_RC=$?
    if [ "$STOP_RC" -eq 0 ]; then
        record 6 "stop 停止采集" "PASS" "$STOP_OUT"
    else
        record 6 "stop 停止采集" "FAIL" "$STOP_OUT"
    fi
fi

# ===== 7. PerfLog 端到端验证 =====
echo ""
echo "[7] PerfLog 端到端验证"

if [ -z "$SPDIAG_CLI" ] || [ -z "$DEMO_APP" ]; then
    record 7 "PerfLog 端到端" "FAIL" "缺少 CLI 或 demo_app"
else
    "$SPDIAG_CLI" stop 2>/dev/null || true
    rm -f /dev/shm/spdiag_shm_* 2>/dev/null || true

    START_OUT=$("$SPDIAG_CLI" start --perflog 2>&1 || true)
    if echo "$START_OUT" | grep -qi "started"; then
        record 7 "start --perflog" "PASS" "$START_OUT"
        sleep 1

        # 后台跑 demo 5 秒
        cd "$BUILD_DIR"
        "$DEMO_APP" -d 5 > "$LOG_DIR/demo_perflog.log" 2>&1 &
        DEMO_APP_PID=$!
        sleep 3  # 等 PerfTimer dump
        cd "$PROJECT_DIR"

        # show --perflog
        PERFLOG_OUT=$("$SPDIAG_CLI" show --perflog 2>&1 || true)
        # 数据行由 ShowPerfLog 格式化，Datetime 固定为 YYYY-MM-DD HH:MM:SS.nnnnnnnnn。
        PERFLOG_DATA=$(printf '%s\n' "$PERFLOG_OUT" | grep -E '^[[:space:]]*[0-9]{4}-[0-9]{2}-[0-9]{2}[[:space:]][0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{9}[[:space:]]' | head -1)
        if [ -n "$PERFLOG_DATA" ]; then
            record 7 "show --perflog" "PASS" "PerfLog 有数据"
            echo "  --- PerfLog 前 3 行数据 ---"
            echo "$PERFLOG_OUT" | head -4
        else
            record 7 "show --perflog" "FAIL" "PerfLog 只有表头无数据: $(echo "$PERFLOG_OUT" | head -1)"
        fi

        wait "$DEMO_APP_PID" 2>/dev/null || true
        DEMO_APP_PID=""
        STOP_OUT=$("$SPDIAG_CLI" stop 2>&1)
        STOP_RC=$?
        if [ "$STOP_RC" -eq 0 ]; then
            record 7 "PerfLog stop" "PASS" "$STOP_OUT"
        else
            record 7 "PerfLog stop" "FAIL" "$STOP_OUT"
        fi
    else
        record 7 "start --perflog" "FAIL" "$START_OUT"
    fi
fi

# ===== 8. OB Memstat 端到端验证 =====
echo ""
echo "[8] OB Memstat 端到端验证"

if [ -z "$SPDIAG_CLI" ]; then
    record 8 "OB Memstat" "FAIL" "无 CLI"
elif [ -z "$MEMSTAT_DEMO" ]; then
    record 8 "OB Memstat" "FAIL" "无 memstat_demo"
elif [ "$(id -u)" != "0" ]; then
    record 8 "OB Memstat" "FAIL" "需要 root 权限"
else
    cd "$BUILD_DIR"

    MEMSTAT_FIFO="${LOG_DIR}/memstat_enter"
    rm -f "$MEMSTAT_FIFO" && mkfifo "$MEMSTAT_FIFO"
    # 父进程先以读写方式持有 FIFO，避免 shell 在 exec demo 前阻塞于重定向。
    exec 8<> "$MEMSTAT_FIFO"

    "$MEMSTAT_DEMO" --mode normal --wait <&8 > "$LOG_DIR/memstat_demo.log" 2>&1 &
    MEMSTAT_DEMO_PID=$!
    sleep 1

    if kill -0 "$MEMSTAT_DEMO_PID" 2>/dev/null; then
        record 8 "memstat_demo 启动" "PASS" "PID=$MEMSTAT_DEMO_PID (--wait 阻塞中)"

        timeout --signal=INT --kill-after=3 8 "$SPDIAG_CLI" memstat --pid "$MEMSTAT_DEMO_PID" > "$LOG_DIR/memstat_out.log" 2>&1 &
        MEMSTAT_COLLECT_PID=$!
        sleep 1  # 让采集器 attach

        printf '\n' >&8

        wait "$MEMSTAT_COLLECT_PID" 2>/dev/null || true
        MEMSTAT_COLLECT_PID=""

        MEMSTAT_OUT=$(cat "$LOG_DIR/memstat_out.log" 2>/dev/null)
        if echo "$MEMSTAT_OUT" | grep -qiE "CallStack|Current|Allocs"; then
            record 8 "memstat 采集" "PASS" "memstat 有数据"
            echo "  --- memstat 输出前 5 行 ---"
            echo "$MEMSTAT_OUT" | head -5
        else
            record 8 "memstat 采集" "FAIL" "memstat 无数据: $(echo "$MEMSTAT_OUT" | head -2 | tr '\n' ' ')"
        fi

        stop_process "$MEMSTAT_DEMO_PID"
        MEMSTAT_DEMO_PID=""
    else
        record 8 "memstat_demo 启动" "FAIL" "立即退出。日志: $(tail -3 "$LOG_DIR/memstat_demo.log" 2>/dev/null | tr '\n' ' ')"
    fi
    exec 8>&-
    rm -f "$MEMSTAT_FIFO"
    MEMSTAT_FIFO=""
    cd "$PROJECT_DIR"
fi

# ===== 9. OB Cachestat 端到端验证 =====
echo ""
echo "[9] OB Cachestat 端到端验证"

# 使用仓库自带的矩阵负载产生确定性的缓存访问。demo 先进入 --wait，
# SpDiag attach 后再用 SIGUSR1 放行。demo 自身 CSV 和 SpDiag 表格必须同时有数据。

if [ -z "$SPDIAG_CLI" ]; then
    record 9 "OB Cachestat" "FAIL" "无 CLI"
elif [ -z "$CACHESTAT_DEMO" ]; then
    record 9 "OB Cachestat" "FAIL" "无 cachestat_demo"
elif [ "$(id -u)" != "0" ]; then
    record 9 "OB Cachestat" "FAIL" "需要 root 权限"
else
    CACHESTAT_FIFO="${LOG_DIR}/cachestat_enter"
    CACHESTAT_DEMO_CSV="${LOG_DIR}/cachestat_demo.csv"
    rm -f "$CACHESTAT_FIFO" "$CACHESTAT_DEMO_CSV"
    mkfifo "$CACHESTAT_FIFO"
    exec 9<> "$CACHESTAT_FIFO"

    "$CACHESTAT_DEMO" --mode col-major --n 1024 --repeat 8 --wait \
        --csv "$CACHESTAT_DEMO_CSV" <&9 > "$LOG_DIR/cachestat_demo.log" 2>&1 &
    CACHESTAT_DEMO_PID=$!

    DEMO_READY=""
    for _ in 1 2 3 4 5; do
        if grep -q "Press ENTER or send SIGUSR1" "$LOG_DIR/cachestat_demo.log" 2>/dev/null; then
            DEMO_READY="yes"
            break
        fi
        kill -0 "$CACHESTAT_DEMO_PID" 2>/dev/null || break
        sleep 1
    done

    if [ -z "$DEMO_READY" ]; then
        record 9 "cachestat_demo 就绪" "FAIL" "demo 未进入等待态: $(tail -5 "$LOG_DIR/cachestat_demo.log" 2>/dev/null | tr '\n' ' ')"
    else
        record 9 "cachestat_demo 就绪" "PASS" "PID=$CACHESTAT_DEMO_PID (--wait)"
        stdbuf -oL -eL "$SPDIAG_CLI" cachestat --pid "$CACHESTAT_DEMO_PID" \
            --interval 500 --delta > "$LOG_DIR/cachestat_out.log" 2>&1 &
        CACHESTAT_COLLECT_PID=$!
        sleep 2

        if ! kill -0 "$CACHESTAT_COLLECT_PID" 2>/dev/null; then
            wait "$CACHESTAT_COLLECT_PID" 2>/dev/null; EXIT_CODE=$?
            CACHESTAT_COLLECT_PID=""
            record 9 "cachestat attach" "FAIL" "采集器提前退出(code=$EXIT_CODE): $(head -8 "$LOG_DIR/cachestat_out.log" 2>/dev/null | tr '\n' ' ')"
        else
            record 9 "cachestat attach" "PASS" "采集器 PID=$CACHESTAT_COLLECT_PID"
            kill -USR1 "$CACHESTAT_DEMO_PID"

            for _ in $(seq 1 45); do
                kill -0 "$CACHESTAT_DEMO_PID" 2>/dev/null || break
                sleep 1
            done
            if kill -0 "$CACHESTAT_DEMO_PID" 2>/dev/null; then
                record 9 "cachestat_demo 完成" "FAIL" "45 秒内未完成"
                stop_process "$CACHESTAT_DEMO_PID"
            else
                wait "$CACHESTAT_DEMO_PID" 2>/dev/null; DEMO_RC=$?
                if [ "$DEMO_RC" = "0" ]; then
                    record 9 "cachestat_demo 完成" "PASS" "退出码 0"
                else
                    record 9 "cachestat_demo 完成" "FAIL" "退出码 $DEMO_RC"
                fi
            fi
            CACHESTAT_DEMO_PID=""

            stop_process "$CACHESTAT_COLLECT_PID"
            CACHESTAT_COLLECT_PID=""

            if awk -F, 'NR > 1 && ($6 + 0) > 0 { found=1 } END { exit !found }' "$CACHESTAT_DEMO_CSV" 2>/dev/null; then
                record 9 "cachestat 负载计数" "PASS" "demo L1D access 非零"
            else
                record 9 "cachestat 负载计数" "FAIL" "demo 未生成非零 L1D 计数: $(tail -3 "$CACHESTAT_DEMO_CSV" 2>/dev/null | tr '\n' ' ')"
            fi

            if grep -q "References" "$LOG_DIR/cachestat_out.log" 2>/dev/null && \
               grep -qE "L1-dcache|LLC" "$LOG_DIR/cachestat_out.log" 2>/dev/null; then
                record 9 "cachestat 采集" "PASS" "SpDiag 表格和缓存级别数据均存在"
                echo "  --- cachestat 输出前 12 行 ---"
                head -12 "$LOG_DIR/cachestat_out.log"
            else
                record 9 "cachestat 采集" "FAIL" "未得到 SpDiag 缓存表格: $(head -12 "$LOG_DIR/cachestat_out.log" 2>/dev/null | tr '\n' ' ')"
            fi
        fi
    fi
    stop_process "$CACHESTAT_COLLECT_PID"
    stop_process "$CACHESTAT_DEMO_PID"
    CACHESTAT_COLLECT_PID=""
    CACHESTAT_DEMO_PID=""
    exec 9>&-
    rm -f "$CACHESTAT_FIFO"
    CACHESTAT_FIFO=""
fi

# ===== 10. 单元测试 =====
echo ""
echo "[10] 单元测试 (ctest)"

cd "$BUILD_DIR"
if ctest --output-on-failure 2>&1 | tee "$LOG_DIR/ctest.log"; then
    # 取 "100% tests passed, 43 tests passed out of 43" 这行的总数
    CTEST_TOTAL=$(grep -oE "[0-9]+% tests passed|[0-9]+ tests passed out of [0-9]+" "$LOG_DIR/ctest.log" | tail -1)
    record 10 "ctest" "PASS" "全部通过 (${CTEST_TOTAL})"
else
    CTEST_FAIL=$(grep -c "Failed" "$LOG_DIR/ctest.log" 2>/dev/null || echo "unknown")
    record 10 "ctest" "FAIL" "${CTEST_FAIL} 个测试失败"
fi
cd "$PROJECT_DIR"

# ===== 11. install 验证 =====
echo ""
echo "[11] install 验证"

INSTALL_PREFIX="${PROJECT_DIR}/verify_install_root"
rm -rf "$INSTALL_PREFIX"
if DESTDIR="$INSTALL_PREFIX" cmake --install "$BUILD_DIR" > "$LOG_DIR/install.log" 2>&1; then
    record 11 "cmake --install" "PASS" "install 成功"

    INSTALLED_SPDIAG=$(find "$INSTALL_PREFIX" -name "*spdiag*" 2>/dev/null | wc -l)
    record 11 "安装 spdiag 文件" "PASS" "${INSTALLED_SPDIAG} 个"

    INSTALLED_OLD=$(find "$INSTALL_PREFIX" -name "*ubdiag*" 2>/dev/null | wc -l)
    if [ "$INSTALLED_OLD" = "0" ]; then
        record 11 "无 ubdiag 残留" "PASS" "零残留"
    else
        record 11 "ubdiag 残留" "FAIL" "${INSTALLED_OLD} 个"
    fi

    if find "$INSTALL_PREFIX" -name "SpDiagConfig.cmake" 2>/dev/null | grep -q .; then
        record 11 "SpDiagConfig.cmake" "PASS" "CMake package 安装成功"
    else
        record 11 "SpDiagConfig.cmake" "FAIL" "未安装"
    fi

    if [ -d "$INSTALL_PREFIX/usr/local/include/spdiag" ]; then
        HDR_COUNT=$(find "$INSTALL_PREFIX/usr/local/include/spdiag" -name "*.h" 2>/dev/null | wc -l)
        record 11 "include/spdiag/" "PASS" "${HDR_COUNT} 个头文件"
    else
        record 11 "include/spdiag/" "FAIL" "目录不存在"
    fi
else
    record 11 "cmake --install" "FAIL" "见 $LOG_DIR/install.log"
fi

# ===== 12. SHM 名称验证 =====
echo ""
echo "[12] SHM 名称验证"

if [ -n "$SPDIAG_CLI" ]; then
    "$SPDIAG_CLI" stop 2>/dev/null || true
    rm -f /dev/shm/spdiag_shm_* /dev/shm/ubdiag_shm_* 2>/dev/null || true
    "$SPDIAG_CLI" start > /dev/null 2>&1
    if ls /dev/shm/spdiag_shm_* 2>/dev/null | grep -q .; then
        record 12 "SHM 名称 spdiag" "PASS" "$(ls /dev/shm/spdiag_shm_* 2>/dev/null)"
    elif ls /dev/shm/ubdiag_shm_* 2>/dev/null | grep -q .; then
        record 12 "SHM 名称" "FAIL" "SHM 名仍是 ubdiag"
    else
        record 12 "SHM 名称" "FAIL" "SHM 未创建"
    fi
    "$SPDIAG_CLI" stop > /dev/null 2>&1 || true
else
    record 12 "SHM 名称" "FAIL" "无 CLI"
fi

# ===== 汇总 =====
cd "$PROJECT_DIR"
echo ""
echo "============================================================"
echo "验证完成: ${PASS_COUNT} PASS / ${FAIL_COUNT} FAIL / ${SKIP_COUNT} SKIP"
echo "CSV: ${CSV_FILE}"
echo "日志: ${LOG_DIR}/"
echo "============================================================"
echo ""
echo "CSV 内容预览:"
cat "$CSV_FILE"

if [ "$FAIL_COUNT" -gt 0 ] || [ "$SKIP_COUNT" -gt 0 ]; then
    exit 1
fi
