#!/bin/bash
# Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
# SpDiag is licensed under Mulan PSL v2.

# Real Git archives and a fake rpmbuild validate packaging orchestration.
# Installed SDK contents are checked separately by test_install_consumer.
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
REPO="$TEST_DIR/source tree's"
export RPM_TOPDIR="$TEST_DIR/rpm work"
export RPM_ARGS_LOG="$TEST_DIR/rpm-args"
export RPM_CALLS_LOG="$TEST_DIR/rpm-calls"
export RPM_ARCHIVE_LOG="$TEST_DIR/source.tar.gz"
export FORBIDDEN_LOG="$TEST_DIR/forbidden-calls"
mkdir -p "$REPO/scripts/rpm" "$REPO/cmake" "$TEST_DIR/bin"
cp "$PROJECT_DIR/build.sh" "$PROJECT_DIR/spdiag.spec" "$PROJECT_DIR/CMakeLists.txt" "$PROJECT_DIR/.gitattributes" "$REPO/"
cp "$PROJECT_DIR/scripts/rpm/package.sh" "$REPO/scripts/rpm/"
cp "$PROJECT_DIR/cmake/source_revision.txt" "$PROJECT_DIR/cmake/SpDiagBuildIdentity.cmake" "$REPO/cmake/"
# CTest can run from an already-expanded SRPM archive. This new Git fixture
# needs its own export-subst template, rather than inheriting that archive SHA.
printf '%s\n' '$Format:%H$' > "$REPO/cmake/source_revision.txt"
git -C "$REPO" init -q
git -C "$REPO" config core.autocrlf false
git -C "$REPO" add .
git -C "$REPO" -c user.name=Test -c user.email=test@example.com -c commit.gpgsign=false commit -qm 'RPM build fixture'
COMMIT="$(git -C "$REPO" rev-parse HEAD)"
export RPM_FIXTURE_VERSION="$(awk '$1 == "Version:" {print $2; exit}' "$REPO/spdiag.spec")"

cat > "$TEST_DIR/bin/rpmbuild" <<'EOF'
#!/bin/bash
set -euo pipefail
printf '%s\n' "$@" > "$RPM_ARGS_LOG"
echo called >> "$RPM_CALLS_LOG"
[ "${RPMBUILD_FAIL:-0}" -eq 0 ] || exit "$RPMBUILD_FAIL"
topdir=""
release=1
for arg in "$@"; do
    case "$arg" in
        '_topdir '*) topdir="${arg#_topdir }" ;;
        'spdiag_release_suffix '*) release+="${arg#spdiag_release_suffix }" ;;
    esac
done
[ -n "$topdir" ]
cp "$topdir/SOURCES/spdiag-$RPM_FIXTURE_VERSION.tar.gz" "$RPM_ARCHIVE_LOG"
sleep "${RPM_BUILD_DELAY:-0}"
mkdir -p "$topdir/RPMS/aarch64" "$topdir/SRPMS"
for name in spdiag spdiag-devel spdiag-static; do
    if [ "$name" = spdiag-static ] && [ "${RPMBUILD_MISSING_STATIC:-0}" -eq 1 ]; then continue; fi
    printf '%s\n' "$release" > "$topdir/RPMS/aarch64/$name-$RPM_FIXTURE_VERSION-$release.aarch64.rpm"
done
touch "$topdir/RPMS/aarch64/spdiag-debuginfo-$RPM_FIXTURE_VERSION-$release.aarch64.rpm"
touch "$topdir/SRPMS/spdiag-$RPM_FIXTURE_VERSION-$release.src.rpm"
EOF
cat > "$TEST_DIR/bin/sudo" <<'EOF'
#!/bin/bash
echo "$0 $*" >> "$FORBIDDEN_LOG"
exit 99
EOF
cp "$TEST_DIR/bin/sudo" "$TEST_DIR/bin/spdiag"
chmod +x "$TEST_DIR/bin/"*
export PATH="$TEST_DIR/bin:$PATH"

fail() { echo "FAIL: $*" >&2; exit 1; }
expect_error() {
    local message="$1"
    shift
    if bash "$REPO/build.sh" "$@" > "$TEST_DIR/error" 2>&1; then fail "unexpected success: $*"; fi
    grep -Fq -- "$message" "$TEST_DIR/error" || { cat "$TEST_DIR/error" >&2; fail "$message"; }
    if grep -Fq 'Build completed successfully.' "$TEST_DIR/error"; then fail 'failed build advertised success'; fi
}
output_dir() { sed -n 's/^RPM output directory: //p' "$1"; }
mkdir -p "$TEST_DIR/command-bin"
export INSTALL_ARGS_LOG="$TEST_DIR/install-args"
cat > "$TEST_DIR/command-bin/sudo" <<'EOF'
#!/bin/bash
set -euo pipefail
[ "$1" = dnf ] || exit 99
shift
printf '%s\0' "$@" > "$INSTALL_ARGS_LOG"
EOF
chmod +x "$TEST_DIR/command-bin/sudo"
check_result() {
    local log="$1" release="$2" out index action
    out="$(output_dir "$log")"
    [[ "$out" == */output/spdiag.* ]] || fail 'missing isolated absolute output directory'
    grep -Fxq "Source commit: $COMMIT" "$log" || fail 'missing source identity'
    local -a commands args
    mapfile -t commands < <(sed -n 's/^  \(sudo dnf .*\)$/\1/p' "$log")
    [ "${#commands[@]}" -eq 6 ] || fail 'expected six install/reinstall commands'
    for index in "${!commands[@]}"; do
        PATH="$TEST_DIR/command-bin:$PATH" bash -c "${commands[index]}"
        action=install
        [ $((index % 2)) -eq 0 ] || action=reinstall
        args=("$action" "$out/spdiag-$RPM_FIXTURE_VERSION-$release.aarch64.rpm")
        [ "$index" -lt 2 ] || args+=("$out/spdiag-devel-$RPM_FIXTURE_VERSION-$release.aarch64.rpm")
        [ "$index" -lt 4 ] || args+=("$out/spdiag-static-$RPM_FIXTURE_VERSION-$release.aarch64.rpm")
        printf '%s\0' "${args[@]}" > "$TEST_DIR/expected"
        cmp "$INSTALL_ARGS_LOG" "$TEST_DIR/expected" || fail 'incorrect install command'
    done
    for name in spdiag spdiag-devel spdiag-static; do
        [ "$(cat "$out/$name-$RPM_FIXTURE_VERSION-$release.aarch64.rpm")" = "$release" ] || fail 'mixed build artifacts'
    done
    [ -f "$out/spdiag-$RPM_FIXTURE_VERSION-$release.src.rpm" ] || fail 'missing source RPM'
}

# Legacy results and local build files must never be deleted or selected.
cd "$TEST_DIR"
mkdir -p "$REPO/build" "$REPO/output" "$RPM_TOPDIR/RPMS"
touch "$REPO/build/keep" "$REPO/untracked" "$REPO/output/spdiag-0.0.0-0.aarch64.rpm" "$RPM_TOPDIR/RPMS/spdiag-legacy.rpm"
bash "$REPO/build.sh" > "$TEST_DIR/default" 2>&1
check_result "$TEST_DIR/default" 1
FIRST_OUTPUT="$(output_dir "$TEST_DIR/default")"
[ -f "$REPO/build/keep" ] && [ -f "$RPM_TOPDIR/RPMS/spdiag-legacy.rpm" ] || fail 'existing build files were removed'
grep -Fxq "spdiag_source_commit $COMMIT" "$RPM_ARGS_LOG" || fail 'source commit not passed to rpmbuild'
tar -tzf "$RPM_ARCHIVE_LOG" > "$TEST_DIR/archive-files"
if grep -q untracked "$TEST_DIR/archive-files"; then fail 'untracked source entered Git archive'; fi
[ "$(tar -xOzf "$RPM_ARCHIVE_LOG" "spdiag-$RPM_FIXTURE_VERSION/cmake/source_revision.txt")" = "$COMMIT" ] || fail 'archive lost source identity'

# An extracted release also builds, and CMake reads the same full commit.
mkdir "$TEST_DIR/release"
tar -xzf "$RPM_ARCHIVE_LOG" -C "$TEST_DIR/release"
ARCHIVE_SRC="$TEST_DIR/release/spdiag-$RPM_FIXTURE_VERSION"
bash "$ARCHIVE_SRC/build.sh" > "$TEST_DIR/archive" 2>&1
check_result "$TEST_DIR/archive" 1
[ "$(tar -xOzf "$RPM_ARCHIVE_LOG" "spdiag-$RPM_FIXTURE_VERSION/cmake/source_revision.txt")" = "$COMMIT" ] || fail 'repacked archive lost source identity'
mkdir -p "$TEST_DIR/identity/cmake"
cp "$ARCHIVE_SRC/cmake/source_revision.txt" "$TEST_DIR/identity/cmake/"
cat > "$TEST_DIR/identity/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.16)
project(Identity LANGUAGES NONE)
include("${IDENTITY_MODULE}")
file(WRITE "${CMAKE_BINARY_DIR}/identity.txt" "${SPDIAG_GIT_SHA}")
EOF
cmake -S "$TEST_DIR/identity" -B "$TEST_DIR/identity-build" \
    "-DIDENTITY_MODULE=$PROJECT_DIR/cmake/SpDiagBuildIdentity.cmake" > "$TEST_DIR/identity.log" 2>&1
[ "$(cat "$TEST_DIR/identity-build/identity.txt")" = "$COMMIT" ] || fail 'CMake lost archive source identity'
# Check the explicit identity supplied by the spec, including validation.
cmake -S "$TEST_DIR/identity" -B "$TEST_DIR/identity-explicit" \
    "-DIDENTITY_MODULE=$PROJECT_DIR/cmake/SpDiagBuildIdentity.cmake" \
    "-DSPDIAG_SOURCE_COMMIT=$COMMIT" > "$TEST_DIR/identity.log" 2>&1
[ "$(cat "$TEST_DIR/identity-explicit/identity.txt")" = "$COMMIT" ] || fail 'CMake ignored explicit identity'
if cmake -S "$TEST_DIR/identity" -B "$TEST_DIR/identity-invalid" \
    "-DIDENTITY_MODULE=$PROJECT_DIR/cmake/SpDiagBuildIdentity.cmake" \
    -DSPDIAG_SOURCE_COMMIT=invalid > "$TEST_DIR/identity.log" 2>&1; then
    fail 'CMake accepted an invalid source identity'
fi

# Feature variants run concurrently with the same work/output roots.
RPM_ARGS_LOG="$TEST_DIR/a.args" RPM_ARCHIVE_LOG="$TEST_DIR/a.tar.gz" RPM_BUILD_DELAY=0.3 \
    bash "$REPO/build.sh" -p on > "$TEST_DIR/a" 2>&1 &
A_PID=$!
RPM_ARGS_LOG="$TEST_DIR/b.args" RPM_ARCHIVE_LOG="$TEST_DIR/b.tar.gz" RPM_BUILD_DELAY=0.3 \
    bash "$REPO/build.sh" -s on > "$TEST_DIR/b" 2>&1 &
B_PID=$!
wait "$A_PID" || fail 'concurrent percentile build failed'
wait "$B_PID" || fail 'concurrent PerfLog build failed'
check_result "$TEST_DIR/a" 1.p99_on
check_result "$TEST_DIR/b" 1.perflog_on
[ "$(output_dir "$TEST_DIR/a")" != "$(output_dir "$TEST_DIR/b")" ] || fail 'shared output directory'
[ -f "$FIRST_OUTPUT/spdiag-$RPM_FIXTURE_VERSION-1.aarch64.rpm" ] || fail 'a later build removed an earlier result'

# Compatibility alias preserves feature macros; per-run paths may differ.
bash "$REPO/build.sh" package -p on -s off -m on -o off -k on > "$TEST_DIR/alias" 2>&1
check_result "$TEST_DIR/alias" 1.p99_on.perflog_off.mempoint_on.obmem_off.obcache_on
for definition in 'spdiag_percentile 1' 'spdiag_perflog 0' 'spdiag_mempoint 1' 'spdiag_ob_memory 0' 'spdiag_ob_cache 1'; do
    grep -Fxq "$definition" "$RPM_ARGS_LOG" || fail "missing macro: $definition"
done
CALLS_BEFORE="$(wc -l < "$RPM_CALLS_LOG")"
bash "$REPO/build.sh" --help > "$TEST_DIR/help"
bash "$REPO/build.sh" package --help > "$TEST_DIR/alias-help"
cmp "$TEST_DIR/help" "$TEST_DIR/alias-help" || fail 'help differs'
for option in -r -c -t -e -i -g -l -f -w -W; do expect_error 'source-build option' "$option" on; done
for option in -p -s -m -o -k; do
    expect_error 'requires on or off' "$option"
    expect_error "expects 'on' or 'off'" "$option" invalid
done
expect_error 'unknown argument' --unknown
[ "$(wc -l < "$RPM_CALLS_LOG")" -eq "$CALLS_BEFORE" ] || fail 'invalid arguments/help invoked rpmbuild'
OUTPUTS_BEFORE="$(find "$REPO/output" -mindepth 1 -maxdepth 1 -type d | wc -l)"
RPMBUILD_MISSING_STATIC=1 expect_error 'missing spdiag, spdiag-devel or spdiag-static RPM'
if RPMBUILD_FAIL=42 bash "$REPO/build.sh" > "$TEST_DIR/error" 2>&1; then fail 'ignored rpmbuild failure'; else
    [ "$?" -eq 42 ] || fail 'rpmbuild exit status changed'
fi
[ "$(find "$REPO/output" -mindepth 1 -maxdepth 1 -type d | wc -l)" -eq "$OUTPUTS_BEFORE" ] || fail 'failed build published artifacts'
[ -z "$(find "$RPM_TOPDIR" -mindepth 1 -maxdepth 1 -type d -name '.spdiag-rpmbuild.*' -print -quit)" ] || fail 'temporary work directory leaked'
printf '\n# dirty fixture\n' >> "$REPO/build.sh"
expect_error 'uncommitted changes'
printf 'invalid\n' > "$ARCHIVE_SRC/cmake/source_revision.txt"
if bash "$ARCHIVE_SRC/build.sh" > "$TEST_DIR/error" 2>&1; then fail 'invalid release identity accepted'; fi
[ ! -e "$FORBIDDEN_LOG" ] || fail 'invoked host install/runtime commands'
echo 'PASS: source identity, Git/release builds, concurrent isolation, install commands and failure checks'
