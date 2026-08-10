#!/usr/bin/env bash
# Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
# SpDiag is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#         http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.

set -uo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/.." && pwd)

SPDIAG_BIN=${SPDIAG_BIN:-}
CSV_EXPORT_DEMO=${CSV_EXPORT_DEMO:-"$REPO_ROOT/build/examples/csv_export_demo"}
MEMPOINT_DEMO=${MEMPOINT_DEMO:-"$REPO_ROOT/build/examples/mempoint_demo"}
OUTPUT_PARENT=""
LOG_FILE=""
VIEW_SECONDS=3
STRICT_OPTIONAL=0

usage() {
    cat <<'EOF'
Usage: scripts/verify_csv_export.sh [options]

Run end-to-end CSV export checks for summary, detail, raw, PerfLog,
history, history detail, memstat, cachestat, and mempoint views.

Options:
  --spdiag <path>         spdiag executable (default: build/src/cli/spdiag or PATH)
  --demo <path>           csv_export_demo executable
  --mempoint-demo <path>  mempoint_demo executable
  --output <dir>          parent directory for this run's artifacts
  --view-seconds <N>      collection time for streaming views (default: 3)
  --strict-optional       treat unavailable optional views as failures
  --log <file>            mirror terminal output to a color-free log
  -h, --help              show this help

Environment variables SPDIAG_BIN, CSV_EXPORT_DEMO, and MEMPOINT_DEMO provide
the same executable overrides as the command-line options.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --spdiag)
            SPDIAG_BIN=$2
            shift 2
            ;;
        --demo)
            CSV_EXPORT_DEMO=$2
            shift 2
            ;;
        --mempoint-demo)
            MEMPOINT_DEMO=$2
            shift 2
            ;;
        --output)
            OUTPUT_PARENT=$2
            shift 2
            ;;
        --view-seconds)
            VIEW_SECONDS=$2
            shift 2
            ;;
        --strict-optional)
            STRICT_OPTIONAL=1
            shift
            ;;
        --log)
            LOG_FILE=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if ! [[ "$VIEW_SECONDS" =~ ^[1-9][0-9]*$ ]]; then
    echo "--view-seconds must be a positive integer" >&2
    exit 2
fi

if [[ -n "$LOG_FILE" ]]; then
    mkdir -p -- "$(dirname -- "$LOG_FILE")"
    exec > >(sed 's/\x1b\[[0-9;]*m//g' | tee -a "$LOG_FILE") 2>&1
fi

if [[ -z "$SPDIAG_BIN" ]]; then
    if [[ -x "$REPO_ROOT/build/src/cli/spdiag" ]]; then
        SPDIAG_BIN="$REPO_ROOT/build/src/cli/spdiag"
    elif command -v spdiag >/dev/null 2>&1; then
        SPDIAG_BIN=$(command -v spdiag)
    else
        echo "Cannot find spdiag; use --spdiag or SPDIAG_BIN" >&2
        exit 1
    fi
elif [[ "$SPDIAG_BIN" != */* ]]; then
    if ! command -v "$SPDIAG_BIN" >/dev/null 2>&1; then
        echo "Cannot find spdiag command: $SPDIAG_BIN" >&2
        exit 1
    fi
    SPDIAG_BIN=$(command -v "$SPDIAG_BIN")
elif [[ ! -x "$SPDIAG_BIN" ]]; then
    echo "spdiag is not executable: $SPDIAG_BIN" >&2
    exit 1
fi

if [[ ! -x "$CSV_EXPORT_DEMO" ]]; then
    echo "csv_export_demo is not executable: $CSV_EXPORT_DEMO" >&2
    echo "Build it with: cmake --build build --target csv_export_demo" >&2
    exit 1
fi

if [[ -n "$OUTPUT_PARENT" ]]; then
    mkdir -p -- "$OUTPUT_PARENT"
    RUN_ROOT="$OUTPUT_PARENT/spdiag-csv-verify-$(date +%Y%m%d_%H%M%S)-$$"
    mkdir -p -- "$RUN_ROOT"
else
    RUN_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/spdiag-csv-verify.XXXXXX")
fi
DATA_DIR="$RUN_ROOT/data"
mkdir -p -- "$DATA_DIR"

if [[ -t 1 ]]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    NC='\033[0m'
else
    RED=''
    GREEN=''
    YELLOW=''
    NC=''
fi

PASS=0
FAIL=0
SKIP=0
DEMO_PID=""
MEMPOINT_PID=""
ACTIVE_VIEW_PID=""

pass() {
    printf '%b[PASS]%b %s\n' "$GREEN" "$NC" "$*"
    PASS=$((PASS + 1))
}

fail() {
    printf '%b[FAIL]%b %s\n' "$RED" "$NC" "$*"
    FAIL=$((FAIL + 1))
}

skip() {
    printf '%b[SKIP]%b %s\n' "$YELLOW" "$NC" "$*"
    SKIP=$((SKIP + 1))
}

info() {
    printf '%b[INFO]%b %s\n' "$YELLOW" "$NC" "$*"
}

show_command_log() {
    local log_file=$1
    local line_count
    line_count=$(wc -l < "$log_file")
    if [[ $line_count -le 40 ]]; then
        cat -- "$log_file"
        return
    fi

    head -n 25 -- "$log_file"
    echo "    ... $((line_count - 35)) lines omitted; full output: $log_file ..."
    tail -n 10 -- "$log_file"
}

stop_pid() {
    local pid=${1:-}
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
        kill -INT "$pid" 2>/dev/null || true
        sleep 1
        kill -TERM "$pid" 2>/dev/null || true
    fi
    if [[ -n "$pid" ]]; then
        wait "$pid" 2>/dev/null || true
    fi
}

cleanup() {
    stop_pid "$ACTIVE_VIEW_PID"
    stop_pid "$MEMPOINT_PID"
    stop_pid "$DEMO_PID"
    "$SPDIAG_BIN" stop >/dev/null 2>&1 || true
}

trap cleanup EXIT
trap 'exit 130' INT TERM

csv_files() {
    local dir=$1
    local prefix=$2
    find "$dir" -maxdepth 1 -type f -name "${prefix}_*.csv" -print 2>/dev/null | sort
}

assert_csv() {
    local dir=$1
    local prefix=$2
    local label=$3
    local required_fields=$4
    local files=()
    mapfile -t files < <(csv_files "$dir" "$prefix")

    if [[ ${#files[@]} -ne 1 ]]; then
        fail "$label expected one ${prefix}_*.csv file, found ${#files[@]}"
        return 1
    fi

    local file=${files[0]}
    local header
    local line_count
    header=$(head -n 1 -- "$file")
    line_count=$(wc -l < "$file")

    if [[ $line_count -lt 2 ]]; then
        fail "$label CSV has no data rows: $file"
        return 1
    fi

    local field
    IFS='|' read -r -a fields <<< "$required_fields"
    for field in "${fields[@]}"; do
        if [[ -n "$field" ]] && ! grep -Fq -- "$field" <<< "$header"; then
            fail "$label CSV header is missing '$field': $header"
            return 1
        fi
    done

    pass "$label generated $(basename -- "$file") with $((line_count - 1)) data rows"
    head -n 3 -- "$file" | sed 's/^/    /'
}

run_one_shot_view() {
    local name=$1
    local prefix=$2
    local fields=$3
    shift 3

    local dir="$RUN_ROOT/$name"
    mkdir -p -- "$dir"
    info "Running: $* --data-dir $DATA_DIR --csv $dir"
    "$@" --data-dir "$DATA_DIR" --csv "$dir" >"$dir/command.log" 2>&1
    local rc=$?
    show_command_log "$dir/command.log"
    if [[ $rc -ne 0 ]]; then
        fail "$name command exited with status $rc"
        return
    fi
    assert_csv "$dir" "$prefix" "$name" "$fields" || true
}

optional_unavailable() {
    local label=$1
    local reason=$2
    if [[ $STRICT_OPTIONAL -eq 1 ]]; then
        fail "$label unavailable: $reason"
    else
        skip "$label unavailable: $reason"
    fi
}

has_command() {
    local command_name=$1
    grep -Eq "^[[:space:]]*${command_name}[[:space:]]" <<< "$HELP_OUTPUT"
}

run_streaming_view() {
    local name=$1
    local prefix=$2
    local fields=$3
    local optional=$4
    shift 4

    local dir="$RUN_ROOT/$name"
    mkdir -p -- "$dir"
    info "Running for ${VIEW_SECONDS}s: $* --data-dir $DATA_DIR --csv $dir"
    "$@" --data-dir "$DATA_DIR" --csv "$dir" >"$dir/command.log" 2>&1 &
    ACTIVE_VIEW_PID=$!
    sleep "$VIEW_SECONDS"
    stop_pid "$ACTIVE_VIEW_PID"
    ACTIVE_VIEW_PID=""
    show_command_log "$dir/command.log"

    local files=()
    mapfile -t files < <(csv_files "$dir" "$prefix")
    if [[ ${#files[@]} -eq 0 && $optional -eq 1 ]]; then
        optional_unavailable "$name" "no CSV was produced; see $dir/command.log"
        return
    fi
    assert_csv "$dir" "$prefix" "$name" "$fields" || true
}

echo "========================================="
echo " SpDiag CSV export verification"
echo "========================================="
echo "spdiag:  $SPDIAG_BIN"
echo "demo:    $CSV_EXPORT_DEMO"
echo "output:  $RUN_ROOT"
echo "data:    $DATA_DIR"
echo

"$SPDIAG_BIN" stop >/dev/null 2>&1 || true

HELP_OUTPUT=$("$SPDIAG_BIN" --help 2>&1 || true)
HAS_PERFLOG=0
if grep -q -- '--perflog' <<< "$HELP_OUTPUT"; then
    if "$SPDIAG_BIN" start --perflog --data-dir "$DATA_DIR" >"$RUN_ROOT/start.log" 2>&1; then
        HAS_PERFLOG=1
        pass "started SHM with PerfLog enabled"
    else
        cat -- "$RUN_ROOT/start.log"
        info "PerfLog start failed; retrying without PerfLog"
        "$SPDIAG_BIN" start --data-dir "$DATA_DIR" >"$RUN_ROOT/start.log" 2>&1
        if [[ $? -ne 0 ]]; then
            cat -- "$RUN_ROOT/start.log"
            fail "spdiag start failed"
            exit 1
        fi
        optional_unavailable "perflog" "spdiag start --perflog failed"
    fi
else
    "$SPDIAG_BIN" start --data-dir "$DATA_DIR" >"$RUN_ROOT/start.log" 2>&1
    if [[ $? -ne 0 ]]; then
        cat -- "$RUN_ROOT/start.log"
        fail "spdiag start failed"
        exit 1
    fi
    optional_unavailable "perflog" "binary was built without PerfLog"
fi

"$CSV_EXPORT_DEMO" --duration 0 --threads 3 >"$RUN_ROOT/csv_export_demo.log" 2>&1 &
DEMO_PID=$!
sleep 3
if ! kill -0 "$DEMO_PID" 2>/dev/null; then
    cat -- "$RUN_ROOT/csv_export_demo.log"
    fail "csv_export_demo exited before verification started"
    exit 1
fi
pass "csv_export_demo is running with pid $DEMO_PID"

run_one_shot_view "summary" "spdiag_perfpoint_summary" "Program|Module|Point" \
    "$SPDIAG_BIN" show
run_one_shot_view "detail" "spdiag_perfpoint_detail" "Core|Program|Name" \
    "$SPDIAG_BIN" show --detail

DETAIL_FILES=()
mapfile -t DETAIL_FILES < <(csv_files "$RUN_ROOT/detail" "spdiag_perfpoint_detail")
RAW_CORE=""
if [[ ${#DETAIL_FILES[@]} -eq 1 ]]; then
    RAW_CORE=$(awk -F',' 'NR == 2 { print $2; exit }' "${DETAIL_FILES[0]}")
fi
if [[ "$RAW_CORE" =~ ^[0-9]+$ ]]; then
    run_one_shot_view "raw" "spdiag_rawtable" "Core|Program|Name" \
        "$SPDIAG_BIN" show --core "$RAW_CORE"
else
    fail "cannot select a populated core from detail CSV"
fi

if [[ $HAS_PERFLOG -eq 1 ]]; then
    run_one_shot_view "perflog" "spdiag_perflog" "Datetime|PID|Cost(ns)" \
        "$SPDIAG_BIN" show --perflog
fi

run_one_shot_view "history" "spdiag_history" "Timestamp(ns)|Module.Point" \
    "$SPDIAG_BIN" history
run_one_shot_view "history_detail" "spdiag_history_detail" "Module|Point" \
    "$SPDIAG_BIN" history -i 0

if has_command "memstat"; then
    run_streaming_view "memstat" "spdiag_memstat" "PID|TID" 1 \
        "$SPDIAG_BIN" memstat --pid "$DEMO_PID" --interval 500
else
    optional_unavailable "memstat" "binary was built without the memstat command"
fi

if has_command "cachestat"; then
    run_streaming_view "cachestat" "spdiag_cachestat" "Level|References|Misses" 1 \
        "$SPDIAG_BIN" cachestat --pid "$DEMO_PID" --interval 500
else
    optional_unavailable "cachestat" "binary was built without the cachestat command"
fi

if ! has_command "mempoint"; then
    optional_unavailable "mempoint" "binary was built without the mempoint command"
elif [[ -x "$MEMPOINT_DEMO" ]]; then
    "$MEMPOINT_DEMO" --mode tracked_vector --count 1000 --loop --interval 100 \
        >"$RUN_ROOT/mempoint_demo.log" 2>&1 &
    MEMPOINT_PID=$!
    sleep 2
    if kill -0 "$MEMPOINT_PID" 2>/dev/null; then
        run_streaming_view "mempoint" "spdiag_mempoint" "Category|Key" 1 \
            "$SPDIAG_BIN" mempoint --pid "$MEMPOINT_PID" --interval 500
    else
        optional_unavailable "mempoint" "mempoint_demo exited before collection"
    fi
else
    optional_unavailable "mempoint" "mempoint_demo is not executable: $MEMPOINT_DEMO"
fi

echo
echo "========================================="
echo " Results: PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
echo " Artifacts: $RUN_ROOT"
echo "========================================="

if [[ $FAIL -ne 0 ]]; then
    exit 1
fi
