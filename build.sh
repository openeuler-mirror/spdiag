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

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ "${1:-}" = "package" ]; then
    shift
    exec bash "$SCRIPT_DIR/scripts/rpm/package.sh" "$@"
fi

BUILD_DIR="build"
BUILD_TYPE="Debug"
CLEAN=true
BUILD_TESTS=false
BUILD_EXAMPLES=true
INSTALL=true
GTEST_SRC=""
ENABLE_PERCENTILE=false
ENABLE_PERFLOG=false
ENABLE_MEMPOINT=false
ENABLE_OB_MEMORY=true
ENABLE_OB_CACHE=true
DEBUG_FRAMES=false
SHARED=false
LOG_LEVEL="ERROR"
LOG_TO_FILE=false
LOG_TO_CONSOLE=true

usage() {
    cat <<'EOF'
Usage:
  bash build.sh [options]
  bash build.sh package [package-options]

Commands:
  package        Build Release RPM package(s) into output/
                 Run 'bash build.sh package --help' for package feature options

Options:
  -r             Release mode (default: Debug)
  -c on|off      Clean build directory before building (default: on)
  -t on|off      Build unit tests (default: off)
  -e on|off      Build examples (default: on)
  -i on|off      Install to system (default: on)
  -g <path>      Specify local googletest source directory
  -l on|off      Build as shared library (.so) instead of static (.a) (default: off)
  -p on|off      Enable percentile (P99/P999/P9999) calculation (default: off)
  -s on|off      Enable PerfLog timestamp logging (default: off)
  -m on|off      Enable MemPoint point-level memory observation (default: off)
  -o on|off      Enable OB memory allocation tracking via eBPF (default: on)
  -k on|off      Enable OB cachestat: cache-references/cache-misses via perf_event (default: on)
  -f on|off      Enable frame resolution debug output (default: off)

Logging Options:
  -d LEVEL       Set log level (DEBUG, INFO, WARN, ERROR) [default: ERROR]
  -w on|off      Enable log to console [default: on]
  -W on|off      Enable log to file [default: off]

  -h, --help     Show this help

Examples:
  bash build.sh package           # Build RPM package(s)
  bash build.sh package -p on     # Build RPM with percentile enabled
  bash build.sh -r                # Release build
  bash build.sh -t on             # Build with tests
  bash build.sh -e off            # Skip examples (CLI only)
  bash build.sh -i off            # Skip system install
  bash build.sh -p on             # Enable percentile calculation
  bash build.sh -s on             # Enable PerfLog timestamp logging
  bash build.sh -m on             # Enable MemPoint
  bash build.sh -d DEBUG          # Enable DEBUG level logging
  bash build.sh -d INFO -W on     # INFO level with file output
EOF
    exit 0
}

# Detect whether SpDiag shared memory is currently live.
#
# `spdiag start` creates a POSIX shared-memory segment (e.g. /dev/shm/spdiag_shm_*)
# that stays mapped until `spdiag stop`. Recompiling/reinstalling while it is
# present lets the freshly built `spdiag show` / `spdiag stop` interpret the old
# SHM with a possibly different layout, causing out-of-bounds reads and a
# segmentation fault (core dumped). Refuse to build until it is stopped.
#
# Returns 0 (and asks) when live SHM is found, 1 when nothing is running.
check_spdiag_running() {
    # POSIX shared memory is Linux-only; nothing to guard otherwise.
    # [ -d /dev/shm ] || return 0

    shopt -s nullglob
    local shm_files=(/dev/shm/spdiag_shm*)
    shopt -u nullglob

    [ ${#shm_files[@]} -eq 0 ] && return 0

    echo ""
    echo "WARNING: SpDiag shared memory is currently live:"
    printf '  - %s\n' "${shm_files[@]}"
    echo "Rebuilding while SpDiag is running can make 'spdiag show'/'spdiag stop'"
    echo "from the new build segfault (shared-memory layout / out-of-bounds mismatch)."
    echo ""
    printf "Run 'spdiag stop' now and continue building? [y/N] "

    local reply
    read -r reply
    case "$reply" in
        y|Y|yes|YES)
            echo "Stopping SpDiag..."
            if command -v spdiag >/dev/null 2>&1; then
                spdiag stop || true
            else
                echo "build.sh: 'spdiag' not on PATH; unlinking SHM directly." >&2
            fi
            # Safety net: drop any lingering segment the binary could not clear.
            rm -f "${shm_files[@]}" 2>/dev/null || true

            # Abort if anything survived (e.g. root-owned SHM that needs sudo).
            shopt -s nullglob
            local leftover=(/dev/shm/spdiag_shm*)
            shopt -u nullglob
            if [ ${#leftover[@]} -gt 0 ]; then
                echo "build.sh: could not clear SpDiag SHM" >&2
                echo "  (it may be root-owned; try 'sudo spdiag stop' or 'sudo rm ${leftover[*]}')." >&2
                echo "Aborting build." >&2
                exit 1
            fi
            echo "SpDiag stopped. Continuing build..."
            ;;
        *)
            echo "build.sh: aborting. Stop SpDiag first ('spdiag stop'), then rebuild." >&2
            exit 1
            ;;
    esac
}

while [[ $# -gt 0 ]]; do
    case $1 in
        -r) BUILD_TYPE="Release"; shift ;;
        -c) case "$2" in
                on)  CLEAN=true;  shift 2 ;;
                off) CLEAN=false; shift 2 ;;
                *)   echo "build.sh: -c expects 'on' or 'off', got '$2'"; exit 1 ;;
            esac ;;
        -t) case "$2" in
                on)  BUILD_TESTS=true;  shift 2 ;;
                off) BUILD_TESTS=false; shift 2 ;;
                *)   echo "build.sh: -t expects 'on' or 'off', got '$2'"; exit 1 ;;
            esac ;;
        -e) case "$2" in
                on)  BUILD_EXAMPLES=true;  shift 2 ;;
                off) BUILD_EXAMPLES=false; shift 2 ;;
                *)   echo "build.sh: -e expects 'on' or 'off', got '$2'"; exit 1 ;;
            esac ;;
        -i) case "$2" in
                on)  INSTALL=true;  shift 2 ;;
                off) INSTALL=false; shift 2 ;;
                *)   echo "build.sh: -i expects 'on' or 'off', got '$2'"; exit 1 ;;
            esac ;;
        -p) case "$2" in
                on)  ENABLE_PERCENTILE=true;  shift 2 ;;
                off) ENABLE_PERCENTILE=false; shift 2 ;;
                *)   echo "build.sh: -p expects 'on' or 'off', got '$2'"; exit 1 ;;
            esac ;;
        -s) case "$2" in
                on)  ENABLE_PERFLOG=true;  shift 2 ;;
                off) ENABLE_PERFLOG=false; shift 2 ;;
                *)   echo "build.sh: -s expects 'on' or 'off', got '$2'"; exit 1 ;;
            esac ;;
        -m) case "$2" in
                on)  ENABLE_MEMPOINT=true;  shift 2 ;;
                off) ENABLE_MEMPOINT=false; shift 2 ;;
                *)   echo "build.sh: -m expects 'on' or 'off', got '$2'"; exit 1 ;;
            esac ;;
        -o) case "$2" in
                on)  ENABLE_OB_MEMORY=true;  shift 2 ;;
                off) ENABLE_OB_MEMORY=false; shift 2 ;;
                *)   echo "build.sh: -o expects 'on' or 'off', got '$2'"; exit 1 ;;
            esac ;;
        -k) case "$2" in
                on)  ENABLE_OB_CACHE=true;  shift 2 ;;
                off) ENABLE_OB_CACHE=false; shift 2 ;;
                *)   echo "build.sh: -k expects 'on' or 'off', got '$2'"; exit 1 ;;
            esac ;;
        -f) case "$2" in
                on)  DEBUG_FRAMES=true;  shift 2 ;;
                off) DEBUG_FRAMES=false; shift 2 ;;
                *)   echo "build.sh: -f expects 'on' or 'off', got '$2'"; exit 1 ;;
            esac ;;
        -l) case "$2" in
                on)  SHARED=true;  shift 2 ;;
                off) SHARED=false; shift 2 ;;
                *)   echo "build.sh: -l expects 'on' or 'off', got '$2'"; exit 1 ;;
            esac ;;
        -g) GTEST_SRC="$2"; shift 2 ;;
        -d) LOG_LEVEL="$2"; shift 2 ;;
        -w) case "$2" in
                on)  LOG_TO_CONSOLE=true;  shift 2 ;;
                off) LOG_TO_CONSOLE=false; shift 2 ;;
                *)   echo "build.sh: -w expects 'on' or 'off', got '$2'"; exit 1 ;;
            esac ;;
        -W) case "$2" in
                on)  LOG_TO_FILE=true;  shift 2 ;;
                off) LOG_TO_FILE=false; shift 2 ;;
                *)   echo "build.sh: -W expects 'on' or 'off', got '$2'"; exit 1 ;;
            esac ;;
        -h|--help) usage ;;
        *) echo "Unknown argument: $1"; usage ;;
    esac
done

cd "$SCRIPT_DIR"

check_spdiag_running

if [ "$CLEAN" = true ]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "Configuring SpDiag (Build Type: $BUILD_TYPE)..."

CMAKE_ARGS="-DCMAKE_BUILD_TYPE=$BUILD_TYPE"
CMAKE_ARGS="$CMAKE_ARGS -DBUILD_TESTS=$( [ "$BUILD_TESTS" = true ] && echo ON || echo OFF )"
CMAKE_ARGS="$CMAKE_ARGS -DBUILD_EXAMPLES=$( [ "$BUILD_EXAMPLES" = true ] && echo ON || echo OFF )"
CMAKE_ARGS="$CMAKE_ARGS -DENABLE_PERCENTILE=$( [ "$ENABLE_PERCENTILE" = true ] && echo ON || echo OFF )"
CMAKE_ARGS="$CMAKE_ARGS -DENABLE_PERFLOG=$( [ "$ENABLE_PERFLOG" = true ] && echo ON || echo OFF )"
CMAKE_ARGS="$CMAKE_ARGS -DENABLE_MEMPOINT=$( [ "$ENABLE_MEMPOINT" = true ] && echo ON || echo OFF )"
CMAKE_ARGS="$CMAKE_ARGS -DENABLE_OB_MEMORY=$( [ "$ENABLE_OB_MEMORY" = true ] && echo ON || echo OFF )"
CMAKE_ARGS="$CMAKE_ARGS -DENABLE_OB_CACHE=$( [ "$ENABLE_OB_CACHE" = true ] && echo ON || echo OFF )"
CMAKE_ARGS="$CMAKE_ARGS -DSPDIAG_DEBUG_FRAMES=$( [ "$DEBUG_FRAMES" = true ] && echo ON || echo OFF )"
CMAKE_ARGS="$CMAKE_ARGS -DSPDIAG_BUILD_SHARED=$( [ "$SHARED" = true ] && echo ON || echo OFF )"
CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_POSITION_INDEPENDENT_CODE=ON"
CMAKE_ARGS="$CMAKE_ARGS -DSPDIAG_LOG_LEVEL=$LOG_LEVEL"
CMAKE_ARGS="$CMAKE_ARGS -DSPDIAG_LOG_TO_CONSOLE=$( [ "$LOG_TO_CONSOLE" = true ] && echo ON || echo OFF )"
CMAKE_ARGS="$CMAKE_ARGS -DSPDIAG_LOG_TO_FILE=$( [ "$LOG_TO_FILE" = true ] && echo ON || echo OFF )"

if [ "$BUILD_TESTS" = true ] && [ -n "$GTEST_SRC" ]; then
    export SPDIAG_GTEST_SOURCE="$GTEST_SRC"
    echo "Using local GTest source: $GTEST_SRC"
fi

rm -rf /tmp/spdiag_data/

cmake $CMAKE_ARGS ..

echo "Building SpDiag..."
cmake --build . --config "$BUILD_TYPE" --parallel

if [ "$INSTALL" = true ]; then
    echo ""
    echo "Installing spdiag to system..."
    if [ "$(id -u)" -ne 0 ]; then
        echo "Installing to /usr/local requires root privileges, using sudo..."
        sudo cmake --install .
    else
        cmake --install .
    fi
    echo "Installation completed!"
fi

echo ""
echo "=========================================="
echo "Build completed successfully!"
echo "=========================================="

if [ "$BUILD_TESTS" = true ]; then
    echo ""
    echo "To run tests:"
    echo "  cd build && ctest --output-on-failure"
    echo "  cd build && ./tests/test_aggregator    # run a single test"
fi

if [ "$BUILD_TESTS" = true ] && [ "$INSTALL" = true ]; then
    echo ""
    echo "Building user_def_test..."
    USER_TEST_DIR="$SCRIPT_DIR/tests/user_def_test"
    mkdir -p "$USER_TEST_DIR/build"
    (cd "$USER_TEST_DIR/build" && cmake .. && cmake --build . -j$(nproc))
fi

echo ""
echo "PerfPoint Demo:"
echo "  Terminal 1: sudo spdiag start"
echo "  Terminal 2: ./build/examples/demo_app -d 10"
echo "  Terminal 3: spdiag show              # or: spdiag watch"

if [ "$ENABLE_OB_MEMORY" = true ]; then
echo ""
echo "Memory Observation (memstat, Linux only):"
echo "  Terminal 1: ./build/examples/memstat_demo --wait"
echo "  Terminal 2: sudo spdiag memstat --pid <PID>"
fi

if [ "$ENABLE_OB_CACHE" = true ]; then
echo ""
echo "Cache Observation (cachestat, Linux only):"
echo "  Terminal 1: ./build/examples/cachestat_demo --mode col-major --wait"
echo "  Terminal 2: sudo spdiag cachestat --pid <PID>"
fi

if [ "$ENABLE_PERCENTILE" = true ]; then
echo ""
echo "P99 mode enabled (percentile calculation is ON)"
fi

if [ "$ENABLE_PERFLOG" = true ]; then
echo ""
echo "PerfLog enabled (timestamp logging)"
echo "  Terminal 1: sudo spdiag start --perflog"
echo "  Terminal 2: ./build/examples/demo_app -d 10"
echo "  Terminal 3: spdiag show --perflog"
fi

if [ "$ENABLE_MEMPOINT" = true ]; then
echo ""
echo "MemPoint Observation (Linux only):"
echo "  Terminal 1: ./build/examples/mempoint_demo --wait"
echo "  Terminal 2: sudo spdiag mempoint --pid <PID>"
fi

echo ""
