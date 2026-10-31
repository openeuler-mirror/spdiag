#!/bin/bash
# Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
# SpDiag is licensed under Mulan PSL v2.

# Exercise the public build entry point with real Git archives and a fake
# rpmbuild. This checks routing and arguments, not RPM payloads or compilation.
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
REPO="$TEST_DIR/source tree's"
export RPM_TOPDIR="$TEST_DIR/rpm work"
export RPM_ARGS_LOG="$TEST_DIR/rpm-args"
export RPM_CALLS_LOG="$TEST_DIR/rpm-calls"
export FORBIDDEN_LOG="$TEST_DIR/forbidden-calls"
mkdir -p "$REPO/scripts/rpm" "$TEST_DIR/bin"
cp "$PROJECT_DIR/build.sh" "$PROJECT_DIR/spdiag.spec" "$PROJECT_DIR/CMakeLists.txt" "$REPO/"
cp "$PROJECT_DIR/scripts/rpm/package.sh" "$REPO/scripts/rpm/"
git -C "$REPO" init -q
git -C "$REPO" config core.autocrlf false
git -C "$REPO" add .
git -C "$REPO" -c user.name=Test -c user.email=test@example.com \
    -c commit.gpgsign=false commit -qm 'RPM build fixture'
VERSION="$(awk '$1 == "Version:" { print $2; exit }' "$REPO/spdiag.spec")"
export RPM_FIXTURE_VERSION="$VERSION"

cat > "$TEST_DIR/bin/rpmbuild" <<'EOF'
#!/bin/bash
set -euo pipefail
printf '%s\n' "$@" > "$RPM_ARGS_LOG"
echo called >> "$RPM_CALLS_LOG"
if [ "${RPMBUILD_FAIL:-0}" -ne 0 ]; then
    exit "$RPMBUILD_FAIL"
fi
mkdir -p "$RPM_TOPDIR/RPMS/aarch64" "$RPM_TOPDIR/SRPMS"
for name in spdiag spdiag-devel spdiag-static; do
    if [ "$name" = spdiag-static ] && [ "${RPMBUILD_MISSING_STATIC:-0}" -eq 1 ]; then
        continue
    fi
    touch "$RPM_TOPDIR/RPMS/aarch64/$name-$RPM_FIXTURE_VERSION-1.aarch64.rpm"
done
touch "$RPM_TOPDIR/RPMS/aarch64/spdiag-debuginfo-$RPM_FIXTURE_VERSION-1.aarch64.rpm"
touch "$RPM_TOPDIR/SRPMS/spdiag-$RPM_FIXTURE_VERSION-1.src.rpm"
EOF

# A packaging invocation must never invoke the old host build/install path.
cat > "$TEST_DIR/bin/cmake" <<'EOF'
#!/bin/bash
echo "$0 $*" >> "$FORBIDDEN_LOG"
exit 99
EOF
cp "$TEST_DIR/bin/cmake" "$TEST_DIR/bin/sudo"
cp "$TEST_DIR/bin/cmake" "$TEST_DIR/bin/spdiag"
chmod +x "$TEST_DIR/bin/"*
export PATH="$TEST_DIR/bin:$PATH"

fail() { echo "FAIL: $*" >&2; exit 1; }
expect_error() {
    local expected="$1"
    shift
    if bash "$REPO/build.sh" "$@" > "$TEST_DIR/result" 2>&1; then
        fail "unexpected success: $*"
    fi
    grep -Fq -- "$expected" "$TEST_DIR/result" || {
        cat "$TEST_DIR/result" >&2
        fail "missing diagnostic: $expected"
    }
}

# Call from outside the repository and preserve unrelated local build files.
cd "$TEST_DIR"
mkdir -p "$REPO/build" "$REPO/output"
touch "$REPO/output/spdiag-0.0.0-0.aarch64.rpm"
touch "$REPO/build/keep" "$REPO/untracked"
bash "$REPO/build.sh" > "$TEST_DIR/result" 2>&1
for name in spdiag spdiag-devel spdiag-static; do
    [ -f "$REPO/output/$name-$RPM_FIXTURE_VERSION-1.aarch64.rpm" ] || fail "missing $name RPM"
done
[ -f "$REPO/output/spdiag-$RPM_FIXTURE_VERSION-1.src.rpm" ] || fail 'missing source RPM'
grep -Fxq 'Build completed successfully.' "$TEST_DIR/result" || fail 'missing build completion message'
grep -Fxq "RPM output directory: $REPO/output" "$TEST_DIR/result" || fail 'missing absolute output directory'
for name in spdiag spdiag-devel spdiag-static; do
    grep -Fxq "  $REPO/output/$name-$RPM_FIXTURE_VERSION-1.aarch64.rpm" "$TEST_DIR/result" || fail "missing $name RPM path"
done
grep -Fxq "  $REPO/output/spdiag-$RPM_FIXTURE_VERSION-1.src.rpm" "$TEST_DIR/result" || fail 'missing source RPM path'

# Execute the printed commands against an argument recorder. Spaces and quotes
# in the repository path must survive; stale, debug and source RPMs must stay out.
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
mapfile -t INSTALL_COMMANDS < <(sed -n 's/^  \(sudo dnf .*\)$/\1/p' "$TEST_DIR/result")
[ "${#INSTALL_COMMANDS[@]}" -eq 6 ] || fail 'expected install/reinstall for three package sets'
for index in "${!INSTALL_COMMANDS[@]}"; do
    PATH="$TEST_DIR/command-bin:$PATH" bash -c "${INSTALL_COMMANDS[index]}"
    action=install
    [ $((index % 2)) -eq 0 ] || action=reinstall
    args=("$action" "$REPO/output/spdiag-$RPM_FIXTURE_VERSION-1.aarch64.rpm")
    if [ "$index" -ge 2 ]; then
        args+=("$REPO/output/spdiag-devel-$RPM_FIXTURE_VERSION-1.aarch64.rpm")
    fi
    if [ "$index" -ge 4 ]; then
        args+=("$REPO/output/spdiag-static-$RPM_FIXTURE_VERSION-1.aarch64.rpm")
    fi
    printf '%s\0' "${args[@]}" > "$TEST_DIR/expected-install-args"
    cmp "$INSTALL_ARGS_LOG" "$TEST_DIR/expected-install-args" || fail 'printed command selected the wrong RPMs'
done
[ -f "$REPO/build/keep" ] || fail 'local build directory was cleaned'
grep -Fxq -- "_topdir $RPM_TOPDIR" "$RPM_ARGS_LOG" || fail 'RPM_TOPDIR was not passed intact'
tar -tzf "$RPM_TOPDIR/SOURCES/spdiag-$VERSION.tar.gz" > "$TEST_DIR/archive-files"
grep -Fxq "spdiag-$VERSION/build.sh" "$TEST_DIR/archive-files" || fail 'missing committed source'
if grep -q untracked "$TEST_DIR/archive-files"; then
    fail 'untracked source entered the archive'
fi

# The compatibility alias must be identical, including feature macro arguments.
bash "$REPO/build.sh" -p on -s off -m on -o off -k on > "$TEST_DIR/result" 2>&1
cp "$RPM_ARGS_LOG" "$TEST_DIR/default-args"
bash "$REPO/build.sh" package -p on -s off -m on -o off -k on > "$TEST_DIR/result" 2>&1
cmp "$RPM_ARGS_LOG" "$TEST_DIR/default-args" || fail 'package alias differs'
for definition in 'spdiag_percentile 1' 'spdiag_perflog 0' 'spdiag_mempoint 1' \
    'spdiag_ob_memory 0' 'spdiag_ob_cache 1' \
    'spdiag_release_suffix .p99_on.perflog_off.mempoint_on.obmem_off.obcache_on'; do
    grep -Fxq -- "$definition" "$RPM_ARGS_LOG" || fail "missing macro: $definition"
done

bash "$REPO/build.sh" --help > "$TEST_DIR/help"
bash "$REPO/build.sh" package --help > "$TEST_DIR/alias-help"
cmp "$TEST_DIR/help" "$TEST_DIR/alias-help" || fail 'help differs'
for option in -r -c -t -e -i -g -l -f -w -W; do
    expect_error 'source-build option' "$option" on
done
for option in -p -s -m -o -k; do
    expect_error 'requires on or off' "$option"
    expect_error "expects 'on' or 'off'" "$option" invalid
done
expect_error 'unknown argument' --unknown
RPMBUILD_MISSING_STATIC=1 expect_error 'missing spdiag, spdiag-devel or spdiag-static RPM'
if RPMBUILD_FAIL=42 bash "$REPO/build.sh" > "$TEST_DIR/result" 2>&1; then
    fail 'rpmbuild failure was ignored'
else
    [ "$?" -eq 42 ] || fail 'rpmbuild exit status was not preserved'
fi
if grep -Fq 'Build completed successfully.' "$TEST_DIR/result"; then
    fail 'failed build printed a success message'
fi
printf '\n# dirty fixture\n' >> "$REPO/build.sh"
expect_error 'uncommitted changes'

[ "$(wc -l < "$RPM_CALLS_LOG")" -eq 5 ] || fail 'argument errors/help invoked rpmbuild'
[ ! -e "$FORBIDDEN_LOG" ] || fail 'invoked host build/install commands'
echo 'PASS: RPM-only build, printed install/reinstall commands, archive and failure checks'
