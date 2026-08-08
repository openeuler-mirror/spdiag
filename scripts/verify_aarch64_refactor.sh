#!/usr/bin/env bash
# ============================================================================
# verify_aarch64_refactor.sh
# ----------------------------------------------------------------------------
# SpDiag aarch64-only 整改的全量功能验证脚本。
#
# 在 aarch64 openEuler 主机上执行以下流程：
#   1. 环境前置检查（架构、依赖、SHM 残留）
#   2. 全功能编译（-l on -s on -m on -t on）
#   3. 单元测试（ctest）
#   4. 全量 CLI 命令验证 + CSV 落盘（show/detail/watch/history/memstat/cachestat）
#   5. 产物清单与打包
#
# 用法：
#   ./scripts/verify_aarch64_refactor.sh check         前置检查（只读）
#   ./scripts/verify_aarch64_refactor.sh build         编译
#   ./scripts/verify_aarch64_refactor.sh test          跑 ctest
#   ./scripts/verify_aarch64_refactor.sh functional    跑全量 CLI + CSV 落盘
#   ./scripts/verify_aarch64_refactor.sh all           check+build+test+functional
#   ./scripts/verify_aarch64_refactor.sh status        查看当前状态
#
# 选项：
#   --build-dir PATH    构建目录（默认 ./build）
#   --results-dir PATH  CSV 与日志根目录（默认 ./verify_results/<timestamp>）
#   --proxy URL         下载代理（不传则从 https_proxy 环境变量取；都没有则不走代理）
#   --target-pid N      功能验证目标进程 PID（不传则自动起一个 sleep）
#   --skip-build        all 模式跳过编译（已有 build/ 时）
#   --keep-target       functional 结束后不杀目标进程
#
# 安全策略：
#   - 默认每步独立执行，失败即停（set -euo pipefail）
#   - functional 子命令会自动起停 SHM 与目标进程，结束自动清理
#   - 不修改源码、不修改 git 状态；可与 rollback 脚本配合使用
# ============================================================================

set -euo pipefail

# -------------------- 默认值 --------------------
# 代理：仅从 --proxy 选项或 https_proxy 环境变量取，脚本自身不内置任何具体代理地址。
# 两者都没有时 PROXY 为空，命令中按需决定是否 export。
DEFAULT_PROXY="${https_proxy:-}"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
RESULTS_ROOT="${REPO_ROOT}/verify_results"
PROXY="${DEFAULT_PROXY}"
TARGET_PID=""
SKIP_BUILD=0
KEEP_TARGET=0
RUN_TS=""

# 内部状态
STARTED_SHM=0
SPAWNED_TARGET=0
CSV_ROOT=""

# -------------------- 辅助函数 --------------------
log()  { printf '[verify] %s\n' "$*"; }
warn() { printf '[verify][WARN] %s\n' "$*" >&2; }
die()  { printf '[verify][ERROR] %s\n' "$*" >&2; exit 1; }

# 查找 spdiag CLI 二进制的真实位置。
# CMake 可能输出到 build/spdiag 或 build/src/cli/spdiag（取决于 add_executable 所在目录）。
# 全局变量 SPDIAG_BIN 记录找到的路径，找不到返回空。
find_spdiag_binary() {
    SPDIAG_BIN=""
    for candidate in \
        "${BUILD_DIR}/spdiag" \
        "${BUILD_DIR}/src/cli/spdiag"; do
        if [ -x "$candidate" ]; then
            SPDIAG_BIN="$candidate"
            return 0
        fi
    done
    return 1
}

# 身份验证：确认 SPDIAG_BIN 是当前 HEAD 源码编译的，不是 /usr/local 残留旧版。
# 通过 spdiag --version 打印的 build SHA 与当前 git HEAD SHA 比对。
verify_binary_identity() {
    [ -n "${SPDIAG_BIN}" ] || die "verify_binary_identity: SPDIAG_BIN 未设置"

    # 当前 HEAD SHA（短）
    local head_sha
    head_sha=$(git -C "${REPO_ROOT}" rev-parse --short=8 HEAD 2>/dev/null || echo "unknown")
    local head_short=${head_sha:0:8}

    # 二进制 build SHA（从 --version 输出提取 'build: <sha>' 行）
    local version_out build_sha build_short
    version_out=$("${SPDIAG_BIN}" --version 2>&1 || true)
    build_sha=$(echo "$version_out" | sed -n 's/^build: \([0-9a-f]*\).*/\1/p' | head -1)

    log "二进制身份验证："
    log "  git HEAD SHA   : ${head_sha}"
    log "  binary build SHA: ${build_sha:-（无，可能编译时未启用 _build_info.h）}"

    if [ -z "$build_sha" ]; then
        die "二进制未含构建 SHA（SPDIAG_BUILD_GIT_SHA 未定义）。可能跑的是旧版 spdiag，ctest 结果不可信。请确认重新编译了 cli_config.cpp。"
    fi

    build_short=${build_sha:0:8}
    if [ "$head_short" != "$build_short" ]; then
        die "二进制 SHA (${build_short}) 与当前 HEAD (${head_short}) 不一致。
ctest 跑的不是当前源码编译的 spdiag。可能原因：
  1) /usr/local/bin/spdiag 是旧版，PATH 优先级盖过了 build 产物
  2) build/spdiag 没被重新编译（cmake --build 没覆盖）
  3) 共享库链接到了 /usr/local/lib64/libspdiag.so 旧版
请运行：
  rm -rf ${BUILD_DIR} && bash build.sh -l on -s on -m on -t on
然后重跑 verify。"
    fi

    log "✓ 身份验证通过：二进制来自当前 HEAD ${head_short}"
}

# 清理钩子：脚本退出时确保 SHM 与目标进程被回收（防烧穿）
cleanup() {
    local rc=$?
    if [ "${STARTED_SHM:-0}" = "1" ]; then
        log "清理：停止 SHM"
        "${SPDIAG_BIN:-${BUILD_DIR}/spdiag}" stop >/dev/null 2>&1 || true
    fi
    if [ "${SPAWNED_TARGET:-0}" = "1" ] && [ "${KEEP_TARGET:-0}" = "0" ] && [ -n "${TARGET_PID:-}" ]; then
        log "清理：停止目标进程 PID=${TARGET_PID}"
        kill "${TARGET_PID}" >/dev/null 2>&1 || true
    fi
    if [ -n "${CSV_ROOT:-}" ] && [ -d "${CSV_ROOT:-}" ]; then
        log "本次产物：${CSV_ROOT}"
    fi
    exit $rc
}
trap cleanup EXIT INT TERM

# -------------------- 参数解析 --------------------
parse_args() {
    local subcmd="${1:-}"; shift || true
    SUBCMD="${subcmd}"

    while [ $# -gt 0 ]; do
        case "$1" in
            --build-dir)    BUILD_DIR="$2"; shift 2 ;;
            --results-dir)  RESULTS_ROOT="$2"; shift 2 ;;
            --proxy)        PROXY="$2"; shift 2 ;;
            --target-pid)   TARGET_PID="$2"; shift 2 ;;
            --skip-build)   SKIP_BUILD=1; shift ;;
            --keep-target)  KEEP_TARGET=1; shift ;;
            -h|--help)
                sed -n '2,30p' "$0"
                exit 0
                ;;
            *) die "未知参数：$1（子命令 ${SUBCMD}）" ;;
        esac
    done

    if [ -z "${SUBCMD}" ]; then
        die "请指定子命令：check | build | test | functional | all | status"
    fi
}

# -------------------- 子命令：status --------------------
cmd_status() {
    log "=== aarch64-only 整改验证工具 — 状态 ==="
    echo
    echo "仓库根：${REPO_ROOT}"
    echo "当前分支：$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo '非 git 仓库')"
    echo "当前 HEAD：$(git rev-parse --short HEAD 2>/dev/null || echo '-')"
    echo "构建目录：${BUILD_DIR}$([ -d "${BUILD_DIR}" ] && echo ' [已存在]' || echo ' [未构建]')"
    echo "结果根目录：${RESULTS_ROOT}"
    echo "代理：${PROXY}"
    echo
    if [ -d "${BUILD_DIR}" ]; then
        echo "构建产物："
        for f in spdiag libspdiag.so libspdiag.so.* libspdiag.a; do
            [ -e "${BUILD_DIR}/${f}" ] && echo "  ✓ ${f} ($([ -L "${BUILD_DIR}/${f}" ] && echo symlink || stat -c '%s bytes' "${BUILD_DIR}/${f}" 2>/dev/null || echo '?'))"
        done
    fi
    echo
    if [ -d "${RESULTS_ROOT}" ]; then
        echo "历史验证结果："
        ls -1t "${RESULTS_ROOT}" 2>/dev/null | head -5 | sed 's/^/  /'
    fi
}

# -------------------- 子命令：check（前置检查） --------------------
cmd_check() {
    log "=== 1. 环境前置检查 ==="

    # 架构必须是 aarch64（整改后唯一支持架构）
    local arch
    arch="$(uname -m)"
    [ "${arch}" = "aarch64" ] || die "当前架构 ${arch}，整改后仅支持 aarch64。如需在其它机器跑，先用 rollback 脚本回基线。"
    log "✓ 架构：${arch}"

    # OS（参考环境 openEuler 24.03，但其它 aarch64 Linux 也能跑）
    if [ -f /etc/openEuler-release ]; then
        log "✓ OS：$(cat /etc/openEuler-release)"
    else
        warn "OS 非 openEuler（$(uname -sr)），可能依赖差异，继续..."
    fi

    # 编译工具链
    for tool in cmake gcc g++ clang bpftool; do
        if command -v "${tool}" >/dev/null 2>&1; then
            log "✓ ${tool}：$(${tool} --version 2>&1 | head -1)"
        else
            die "${tool} 未安装。openEuler 24.03 安装命令：sudo dnf install -y cmake gcc-c++ clang bpftool libbpf-devel libelf-devel zlib-devel systemtap-sdt-devel"
        fi
    done

    # 关键头文件
    [ -f /usr/include/sys/sdt.h ] || die "/usr/include/sys/sdt.h 缺失（MemPoint 必需）。装 systemtap-sdt-devel"
    log "✓ sys/sdt.h 存在（MemPoint 可用）"

    # SHM 残留（避免脏启动）
    if ls /dev/shm/spdiag_* >/dev/null 2>&1; then
        warn "/dev/shm 有 spdiag 残留，建议先 spdiag stop 清理："
        ls /dev/shm/spdiag_* | sed 's/^/  /'
    else
        log "✓ /dev/shm 无 spdiag 残留"
    fi

    # git 分支校验（如果在 git 仓库里）
    if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        local branch
        branch="$(git rev-parse --abbrev-ref HEAD)"
        if [ "${branch}" != "feat/aarch64-only-refactor" ]; then
            warn "当前分支 ${branch}，建议在 feat/aarch64-only-refactor 上验证"
        else
            log "✓ git 分支：${branch} @ $(git rev-parse --short HEAD)"
        fi
    fi

    log "前置检查通过。"
}

# -------------------- 子命令：build（编译） --------------------
cmd_build() {
    cmd_check
    log "=== 2. 全功能编译 ==="

    cd "${REPO_ROOT}"

    # 整改后推荐的完整开关：shared lib + PerfLog + MemPoint + tests
    # 对应设计文档 §6.2 第 1 步
    log "构建目录：${BUILD_DIR}"
    log "开关：-l on（shared） -s on（PerfLog） -m on（MemPoint） -t on（tests）"

    bash build.sh -l on -s on -m on -t on

    # 验证产物：查找 spdiag 二进制（CMake 可能输出到 build/ 或 build/src/cli/）
    find_spdiag_binary || die "编译完成但找不到 spdiag CLI 二进制（查找过 ${BUILD_DIR}/spdiag 和 ${BUILD_DIR}/src/cli/spdiag）"
    log "✓ spdiag CLI 已生成：${SPDIAG_BIN}"

    # 身份验证：确认这个二进制是当前 HEAD 编译的，不是 /usr/local 旧版
    verify_binary_identity

    # 链接检查
    log "动态库依赖："
    ldd "${SPDIAG_BIN}" 2>&1 | grep -E 'not found' && die "存在缺失的动态库" || true
    ldd "${SPDIAG_BIN}" 2>&1 | grep -E 'pthread|librt|libbpf|libelf|libz' | sed 's/^/  /' || true
    # 检查 libspdiag.so 链接位置（应是 build/，不是 /usr/local/）
    local lib_path
    lib_path=$(ldd "${SPDIAG_BIN}" 2>&1 | grep 'libspdiag' | head -1)
    if [ -n "$lib_path" ]; then
        log "  libspdiag 链接：${lib_path}"
        echo "$lib_path" | grep -q "/usr/local/" && \
            warn "libspdiag.so 链接到 /usr/local（可能是旧版），建议 rm -rf build/ 重新编译" || true
    fi

    log "✓ 编译通过。"
}

# -------------------- 子命令：test（单元测试） --------------------
cmd_test() {
    [ -d "${BUILD_DIR}" ] || die "构建目录不存在：${BUILD_DIR}，请先跑 build 子命令"
    log "=== 3. 单元测试 ctest ==="

    cd "${BUILD_DIR}"
    ctest --output-on-failure
    log "✓ 单元测试全部通过。"
}

# -------------------- 子命令：functional（全量 CLI + CSV） --------------------
cmd_functional() {
    find_spdiag_binary || die "找不到 spdiag CLI 二进制（${BUILD_DIR}/spdiag 或 ${BUILD_DIR}/src/cli/spdiag）。请先跑 build"
    verify_binary_identity   # 防止"功能跑通但测的是旧版"
    log "=== 4. 全量功能验证 + CSV 落盘 ==="

    # 准备结果目录（带时间戳，反复跑不覆盖）
    RUN_TS="$(date +%Y%m%d_%H%M%S)"
    CSV_ROOT="${RESULTS_ROOT}/run_${RUN_TS}"
    mkdir -p "${CSV_ROOT}/csv"
    log "本次 CSV 根目录：${CSV_ROOT}"

    # ---------- 4.1 启动 SHM ----------
    log "[4.1] 启动 SHM"
    "${SPDIAG_BIN}" start 2>&1 | tee "${CSV_ROOT}/01_start.log" || die "spdiag start 失败"
    STARTED_SHM=1
    sleep 1
    ls /dev/shm/spdiag_* >/dev/null 2>&1 || die "SHM 未创建"
    log "  ✓ SHM 已创建"

    # ---------- 4.2 准备目标进程 ----------
    # 优先用仓库自带的 demo_app（产生 PerfPoint 数据让 show/history 有内容，
    # 产生 CPU+内存+缓存活动让 memstat/cachestat 有数据可采）
    local demo_apps=("${BUILD_DIR}/examples/demo_app" "${BUILD_DIR}/demo_app")
    local demo_app=""
    for d in "${demo_apps[@]}"; do
        if [ -x "$d" ]; then demo_app="$d"; break; fi
    done
    if [ -z "${TARGET_PID}" ]; then
        if [ -n "${demo_app}" ]; then
            log "[4.2] 启动 demo_app -d 120 当被测目标（自带 PerfPoint 打点）"
            # -d 120 跑 2 分钟，适应自动化（-d 0 依赖 stdin，nohup 下立即退出）
            nohup "${demo_app}" -d 120 >/dev/null 2>&1 &
            TARGET_PID=$!
            SPAWNED_TARGET=1
            sleep 2  # demo_app 启动 9 个线程需要一点时间
        else
            warn "${demo_app} 不存在，回退到 python3 sleep（show/history 可能无 PerfPoint 数据）"
            nohup python3 -c "import time; [time.sleep(1) for _ in range(3600)]" >/dev/null 2>&1 &
            TARGET_PID=$!
            SPAWNED_TARGET=1
            sleep 1
        fi
    else
        log "[4.2] 使用指定目标进程 PID=${TARGET_PID}"
        kill -0 "${TARGET_PID}" 2>/dev/null || die "指定的 PID ${TARGET_PID} 不存在"
        SPAWNED_TARGET=0
    fi
    echo "目标进程 PID=${TARGET_PID}" | tee "${CSV_ROOT}/02_target_pid.txt"

    # ---------- 4.3 全功能 CSV 落盘 ----------
    run_cmd() {
        # $1 = 步骤号, $2 = 步骤名, $3.. = 命令
        local step="$1" name="$2"; shift 2
        local csv_dir="${CSV_ROOT}/csv/${name}"
        local log_file="${CSV_ROOT}/${step}_${name}.log"
        mkdir -p "${csv_dir}"
        log "[${step}] ${name} → ${csv_dir}"
        if ! "$@" --csv "${csv_dir}" 2>&1 | tee "${log_file}"; then
            warn "${name} 命令返回非零，继续（部分命令在无数据时可能非零退出）"
        fi
    }

    run_cmd "03" "show"        "${SPDIAG_BIN}" show
    run_cmd "04" "show_detail" "${SPDIAG_BIN}" show -d

    # watch：用 timeout 限时
    log "[05] watch → ${CSV_ROOT}/csv/watch（限时 3s）"
    mkdir -p "${CSV_ROOT}/csv/watch"
    timeout 3 "${SPDIAG_BIN}" watch --csv "${CSV_ROOT}/csv/watch" 2>&1 | tee "${CSV_ROOT}/05_watch.log" || true

    run_cmd "06" "history"     "${SPDIAG_BIN}" history

    # memstat/cachestat 需要目标进程存活（eBPF uprobe + perf_event_open），
    # 前面 show/watch/history 可能耗时较长导致 demo_app 已退出，此处兜底检查重启
    local restart_needed=0
    if ! kill -0 "${TARGET_PID}" 2>/dev/null; then
        warn "目标进程 PID=${TARGET_PID} 已退出，重新启动 demo_app"
        restart_needed=1
    fi
    if [ "${restart_needed}" = "1" ] && [ -n "${demo_app}" ]; then
        nohup "${demo_app}" -d 30 >/dev/null 2>&1 &
        TARGET_PID=$!
        SPAWNED_TARGET=1
        sleep 2
        log "  新目标 PID=${TARGET_PID}"
    fi

    # memstat：后台跑 5 秒采集
    log "[07] memstat → ${CSV_ROOT}/csv/memstat（后台采集 5s）"
    mkdir -p "${CSV_ROOT}/csv/memstat"
    "${SPDIAG_BIN}" memstat --pid "${TARGET_PID}" --csv "${CSV_ROOT}/csv/memstat" \
        > "${CSV_ROOT}/07_memstat.log" 2>&1 &
    local memstat_pid=$!
    sleep 5
    kill "${memstat_pid}" 2>/dev/null || true
    wait "${memstat_pid}" 2>/dev/null || true
    log "  ✓ memstat 采集完成"

    # cachestat：限时 5s
    log "[08] cachestat → ${CSV_ROOT}/csv/cachestat（限时 5s）"
    mkdir -p "${CSV_ROOT}/csv/cachestat"
    timeout 5 "${SPDIAG_BIN}" cachestat --pid "${TARGET_PID}" --csv "${CSV_ROOT}/csv/cachestat" \
        2>&1 | tee "${CSV_ROOT}/08_cachestat.log" || true

    # 触发 dump 后再查 history
    log "[09] 触发 dump（SIGUSR1）"
    local spdiag_pid
    spdiag_pid="$(pgrep -f "${SPDIAG_BIN}" | head -1 || true)"
    if [ -n "${spdiag_pid}" ]; then
        kill -USR1 "${spdiag_pid}" 2>/dev/null || warn "SIGUSR1 发送失败，跳过 dump 触发"
        sleep 1
    else
        warn "无运行中的 spdiag 进程，跳过 dump 触发"
    fi

    run_cmd "10" "history_post" "${SPDIAG_BIN}" history

    # ---------- 4.4 清理 ----------
    log "[11] 停止 SHM"
    "${SPDIAG_BIN}" stop 2>&1 | tee "${CSV_ROOT}/11_stop.log" || true
    STARTED_SHM=0
    if ls /dev/shm/spdiag_* >/dev/null 2>&1; then
        warn "SHM 未自动清理，残留：$(ls /dev/shm/spdiag_*)"
    else
        log "  ✓ SHM 已清理"
    fi

    # ---------- 4.5 CSV 清单 ----------
    log "=== 5. CSV 落盘清单 ==="
    {
        echo "# spdiag aarch64-only 验证 CSV 清单"
        echo "# 生成时间：$(date '+%Y-%m-%d %H:%M:%S')"
        echo "# 仓库 HEAD：$(git rev-parse HEAD 2>/dev/null || echo '-')"
        echo "# 目标 PID：${TARGET_PID}"
        echo
        find "${CSV_ROOT}" -name '*.csv' -printf '%P  %s bytes\n' | sort
        echo
        echo "# 各命令 CSV 数量统计："
        for cmd in show show_detail watch history memstat cachestat history_post; do
            cnt=$(find "${CSV_ROOT}/csv/${cmd}" -name '*.csv' 2>/dev/null | wc -l)
            printf "  %-15s : %d 个 CSV\n" "${cmd}" "${cnt}"
        done
    } | tee "${CSV_ROOT}/CSV_MANIFEST.txt"

    # 打包
    local tarball="${REPO_ROOT}/verify_results_${RUN_TS}.tar.gz"
    tar czf "${tarball}" -C "${RESULTS_ROOT}" "run_${RUN_TS}"
    log "✓ 打包完成：${tarball}"
    log "✓ 功能验证完成。CSV 根目录：${CSV_ROOT}"
}

# -------------------- 子命令：all（全流程） --------------------
cmd_all() {
    if [ "${SKIP_BUILD}" = "1" ]; then
        log "跳过编译（--skip-build）"
        cmd_check
    else
        cmd_build
    fi
    cmd_test
    cmd_functional
}

# -------------------- 入口 --------------------
main() {
    parse_args "$@"
    cd "${REPO_ROOT}"
    case "${SUBCMD}" in
        status)        cmd_status ;;
        check)         cmd_check ;;
        build)         cmd_build ;;
        test)          cmd_test ;;
        functional)    cmd_functional ;;
        all)           cmd_all ;;
        *)
            die "未知子命令：${SUBCMD}。可用：status | check | build | test | functional | all"
            ;;
    esac
}

main "$@"
