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

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODE="${1:-enabled}"

if [ "$MODE" = "disabled" ]; then
    echo "=== Building compile-time disabled user_def_test ==="
    BUILD_DIR="$SCRIPT_DIR/build-disabled"
    cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR" --target user_def_test_disabled -j"$(nproc)"
    echo "  -> user_def_test_disabled built"

    echo ""
    echo "=== Running compile-time disabled test ==="
    "$BUILD_DIR/user_def_test_disabled"

    echo ""
    echo "=== Checking that no SpDiag SDK symbols remain ==="
    command -v nm >/dev/null 2>&1 || {
        echo "ERROR: nm is required to verify the disabled binary" >&2
        exit 1
    }
    if nm -C "$BUILD_DIR/user_def_test_disabled" |
        grep -Eq 'SpDiag::(PerfPoint|PerfManager)'; then
        echo "ERROR: disabled binary still contains PerfPoint or PerfManager symbols" >&2
        nm -C "$BUILD_DIR/user_def_test_disabled" |
            grep -E 'SpDiag::(PerfPoint|PerfManager)' >&2
        exit 1
    fi
    echo "  [PASS] no PerfPoint or PerfManager symbols found"
    exit 0
fi

if [ "$MODE" != "enabled" ]; then
    echo "Usage: $0 [enabled|disabled]" >&2
    exit 2
fi

echo "=== Building user_def_test via find_package ==="
mkdir -p "$SCRIPT_DIR/build"
cd "$SCRIPT_DIR/build"
cmake ..
cmake --build . -j$(nproc)
echo "  -> user_def_test built"

echo ""
echo "=== Running test ==="
./user_def_test
