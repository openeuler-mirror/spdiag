#!/bin/bash
# Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
# SpDiag is licensed under Mulan PSL v2.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
PACKAGE_NAME="spdiag"
SPEC_FILE="$PROJECT_DIR/$PACKAGE_NAME.spec"
RPM_TOPDIR="${RPM_TOPDIR:-$HOME/rpmbuild}"
OUTPUT_DIR="$PROJECT_DIR/output"

# Keep these defaults aligned with spdiag.spec. Only explicitly supplied
# feature switches are included in the RPM Release suffix.
ENABLE_PERCENTILE=0
ENABLE_PERFLOG=0
ENABLE_MEMPOINT=0
ENABLE_OB_MEMORY=1
ENABLE_OB_CACHE=1

SET_PERCENTILE=0
SET_PERFLOG=0
SET_MEMPOINT=0
SET_OB_MEMORY=0
SET_OB_CACHE=0

usage() {
    cat <<'EOF'
Usage:
  bash build.sh package [options]

Options:
  -p on|off      Enable percentile (P99/P999/P9999) calculation (default: off)
  -s on|off      Enable PerfLog timestamp logging (default: off)
  -m on|off      Enable MemPoint point-level memory observation (default: off)
  -o on|off      Enable OB memory allocation tracking via eBPF (default: on)
  -k on|off      Enable OB cache hit rate monitoring via perf_event (default: on)
  -h, --help     Show this help

Only explicitly supplied options are appended to the RPM Release name.
Each build produces both the shared-library packages and spdiag-static.
The package command therefore does not need the build.sh -l option.
Examples:
  bash build.sh package
  bash build.sh package -p on -s on
  bash build.sh package -m on -o off -k off
EOF
}

parse_on_off() {
    local option="$1"
    local value="$2"
    case "$value" in
        on)  echo 1 ;;
        off) echo 0 ;;
        *)
            echo "package.sh: $option expects 'on' or 'off', got '$value'" >&2
            exit 1
            ;;
    esac
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -p)
            [ $# -ge 2 ] || { echo "package.sh: -p requires on or off" >&2; exit 1; }
            ENABLE_PERCENTILE="$(parse_on_off "$1" "$2")"
            SET_PERCENTILE=1
            shift 2
            ;;
        -s)
            [ $# -ge 2 ] || { echo "package.sh: -s requires on or off" >&2; exit 1; }
            ENABLE_PERFLOG="$(parse_on_off "$1" "$2")"
            SET_PERFLOG=1
            shift 2
            ;;
        -m)
            [ $# -ge 2 ] || { echo "package.sh: -m requires on or off" >&2; exit 1; }
            ENABLE_MEMPOINT="$(parse_on_off "$1" "$2")"
            SET_MEMPOINT=1
            shift 2
            ;;
        -o)
            [ $# -ge 2 ] || { echo "package.sh: -o requires on or off" >&2; exit 1; }
            ENABLE_OB_MEMORY="$(parse_on_off "$1" "$2")"
            SET_OB_MEMORY=1
            shift 2
            ;;
        -k)
            [ $# -ge 2 ] || { echo "package.sh: -k requires on or off" >&2; exit 1; }
            ENABLE_OB_CACHE="$(parse_on_off "$1" "$2")"
            SET_OB_CACHE=1
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "package.sh: unknown argument: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

feature_state() {
    if [ "$1" -eq 1 ]; then
        echo "on"
    else
        echo "off"
    fi
}

RELEASE_SUFFIX=""
[ "$SET_PERCENTILE" -eq 0 ] || RELEASE_SUFFIX+=".p99_$(feature_state "$ENABLE_PERCENTILE")"
[ "$SET_PERFLOG" -eq 0 ] || RELEASE_SUFFIX+=".perflog_$(feature_state "$ENABLE_PERFLOG")"
[ "$SET_MEMPOINT" -eq 0 ] || RELEASE_SUFFIX+=".mempoint_$(feature_state "$ENABLE_MEMPOINT")"
[ "$SET_OB_MEMORY" -eq 0 ] || RELEASE_SUFFIX+=".obmem_$(feature_state "$ENABLE_OB_MEMORY")"
[ "$SET_OB_CACHE" -eq 0 ] || RELEASE_SUFFIX+=".obcache_$(feature_state "$ENABLE_OB_CACHE")"

command -v git >/dev/null 2>&1 || {
    echo "package.sh: git is required." >&2
    exit 1
}

GIT_TOPLEVEL="$(git -C "$PROJECT_DIR" rev-parse --show-toplevel 2>/dev/null)" || {
    echo "package.sh: $PROJECT_DIR is not a Git work tree." >&2
    exit 1
}
GIT_TOPLEVEL="$(cd "$GIT_TOPLEVEL" && pwd)"
if [ "$GIT_TOPLEVEL" != "$PROJECT_DIR" ]; then
    echo "package.sh: project directory must be the Git repository root: $PROJECT_DIR" >&2
    exit 1
fi

if ! git -C "$PROJECT_DIR" diff --quiet --ignore-submodules -- ||
   ! git -C "$PROJECT_DIR" diff --cached --quiet --ignore-submodules --; then
    echo "package.sh: tracked files contain uncommitted changes." >&2
    echo "Commit or stash them before building a reproducible RPM:" >&2
    git -C "$PROJECT_DIR" status --short --untracked-files=no >&2
    exit 1
fi

[ -f "$SPEC_FILE" ] || {
    echo "package.sh: spec file not found: $SPEC_FILE" >&2
    exit 1
}

VERSION="$(awk '$1 == "Version:" { print $2; exit }' "$SPEC_FILE")"
[ -n "$VERSION" ] || {
    echo "package.sh: failed to read Version from $SPEC_FILE" >&2
    exit 1
}

CMAKE_VERSION="$(awk '
    /^[[:space:]]*project\(SpDiag[[:space:]]+VERSION[[:space:]]+/ {
        for (i = 1; i <= NF; i++) {
            if ($i == "VERSION") {
                version = $(i + 1)
                sub(/\).*/, "", version)
                print version
                exit
            }
        }
    }
' "$PROJECT_DIR/CMakeLists.txt")"
if [ -n "$CMAKE_VERSION" ] && [ "$CMAKE_VERSION" != "$VERSION" ]; then
    echo "package.sh: version mismatch: spdiag.spec=$VERSION, CMakeLists.txt=$CMAKE_VERSION" >&2
    exit 1
fi

command -v rpmbuild >/dev/null 2>&1 || {
    echo "package.sh: rpmbuild is required (install rpm-build first)." >&2
    exit 1
}

SOURCE_DIR_NAME="$PACKAGE_NAME-$VERSION"
SOURCE_PACKAGE="$SOURCE_DIR_NAME.tar.gz"
SOURCE_COMMIT="$(git -C "$PROJECT_DIR" rev-parse HEAD)"

mkdir -p "$RPM_TOPDIR"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS} "$OUTPUT_DIR"

echo "Creating source archive from Git commit $SOURCE_COMMIT:"
echo "  $RPM_TOPDIR/SOURCES/$SOURCE_PACKAGE"
git -C "$PROJECT_DIR" archive \
    --format=tar.gz \
    --prefix="$SOURCE_DIR_NAME/" \
    --output="$RPM_TOPDIR/SOURCES/$SOURCE_PACKAGE" \
    "$SOURCE_COMMIT"

cp -f "$SPEC_FILE" "$RPM_TOPDIR/SPECS/$PACKAGE_NAME.spec"

find "$RPM_TOPDIR/RPMS" -type f -name "$PACKAGE_NAME-*.rpm" -delete
find "$RPM_TOPDIR/SRPMS" -type f -name "$PACKAGE_NAME-*.src.rpm" -delete

echo "Building source and binary RPM package(s)..."
RPMBUILD_ARGS=(
    -ba
    --clean
    --define "_topdir $RPM_TOPDIR"
    --define "spdiag_percentile $ENABLE_PERCENTILE"
    --define "spdiag_perflog $ENABLE_PERFLOG"
    --define "spdiag_mempoint $ENABLE_MEMPOINT"
    --define "spdiag_ob_memory $ENABLE_OB_MEMORY"
    --define "spdiag_ob_cache $ENABLE_OB_CACHE"
)
if [ -n "$RELEASE_SUFFIX" ]; then
    RPMBUILD_ARGS+=(--define "spdiag_release_suffix $RELEASE_SUFFIX")
fi
rpmbuild "${RPMBUILD_ARGS[@]}" "$RPM_TOPDIR/SPECS/$PACKAGE_NAME.spec"

mapfile -d '' RPM_FILES < <(find "$RPM_TOPDIR/RPMS" -type f -name "$PACKAGE_NAME-*.rpm" -print0)
if [ "${#RPM_FILES[@]}" -eq 0 ]; then
    echo "package.sh: rpmbuild completed but no RPM files were found." >&2
    exit 1
fi
mapfile -d '' SOURCE_RPM_FILES < <(find "$RPM_TOPDIR/SRPMS" -type f -name "$PACKAGE_NAME-*.src.rpm" -print0)
if [ "${#SOURCE_RPM_FILES[@]}" -eq 0 ]; then
    echo "package.sh: rpmbuild completed but no source RPM was found." >&2
    exit 1
fi

echo "RPM package(s) written to:"
for rpm_file in "${RPM_FILES[@]}" "${SOURCE_RPM_FILES[@]}"; do
    cp -p "$rpm_file" "$OUTPUT_DIR/"
    echo "  $OUTPUT_DIR/$(basename "$rpm_file")"
done
