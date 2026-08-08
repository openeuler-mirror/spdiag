#!/usr/bin/env bash
# ============================================================================
# aarch64_refactor_rollback.sh
# ----------------------------------------------------------------------------
# SpDiag aarch64-only 整改的一键回退工具。
#
# 对应设计文档：docs/architecture/spdiag_aarch64_only_design_v1.2.md §9
#
# 用法：
#   ./scripts/aarch64_refactor_rollback.sh patch  [--out PATH]   生成 patch
#   ./scripts/aarch64_refactor_rollback.sh revert [--apply]      回退整改
#   ./scripts/aarch64_refactor_rollback.sh verify                验证回退结果
#   ./scripts/aarch64_refactor_rollback.sh status                查看当前状态
#
# 安全策略：
#   - 默认 dry-run（revert 子命令需显式 --apply 才真改文件）
#   - patch 默认输出到仓库外（../），避免污染工作区
#   - 执行前校验分支、基线、工作区状态
# ============================================================================

set -euo pipefail

# -------------------- 常量 --------------------
# 整改前的基线 commit。变更此值需同步更新设计文档 §9.1
BASELINE_COMMIT="d4ba3f4c0df6023c0bf4a6bee685d8e7ac016696"
BASELINE_SHORT="d4ba3f4"
EXPECTED_BRANCH="feat/aarch64-only-refactor"

# 仓库根目录（脚本位于 scripts/ 下）
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# -------------------- 辅助函数 --------------------
log()  { printf '[rollback] %s\n' "$*"; }
warn() { printf '[rollback][WARN] %s\n' "$*" >&2; }
die()  { printf '[rollback][ERROR] %s\n' "$*" >&2; exit 1; }

# 校验是否在正确的 git 仓库与分支
check_prerequisites() {
    command -v git >/dev/null 2>&1 || die "git 未安装"

    [ -d ".git" ] || die "当前目录不是 git 仓库根：$REPO_ROOT"

    local current_branch
    current_branch="$(git rev-parse --abbrev-ref HEAD)"
    if [ "$current_branch" != "$EXPECTED_BRANCH" ]; then
        die "当前分支 '$current_branch'，应为 '$EXPECTED_BRANCH'。请先 git checkout $EXPECTED_BRANCH"
    fi

    # 基线 commit 必须可达
    if ! git merge-base --is-ancestor "$BASELINE_COMMIT" HEAD 2>/dev/null; then
        die "基线 commit $BASELINE_SHORT 在当前分支历史中不可达。分支可能已被改写。"
    fi

    log "前置校验通过：分支=$current_branch，基线=$BASELINE_SHORT 可达"
}

# 工作区是否干净（无未提交改动）
worktree_clean() {
    [ -z "$(git status --porcelain)" ]
}

# 整改涉及的源码路径（与 §6.3 残留 grep 对应）
AFFECTED_PATHS=(
    "CMakeLists.txt"
    "src/sdk/CMakeLists.txt"
    "src/runtime/CMakeLists.txt"
    "src/runtime/ebpf/CMakeLists.txt"
    "src/runtime/plugins/ob_point/CMakeLists.txt"
    "src/runtime/plugins/ob_point/bpf/ob_point.bpf.c"
    "src/platform/linux/platform_linux.cpp"
    "src/sdk/perf_point.cpp"
    "src/manager/manager.cpp"
    "include/spdiag/platform.h"
    "src/sdk/platform.h"
)

# -------------------- 子命令：status --------------------
cmd_status() {
    log "=== aarch64-only 整改回退工具 — 状态 ==="
    echo
    echo "仓库根：$REPO_ROOT"
    echo "当前分支：$(git rev-parse --abbrev-ref HEAD)"
    echo "当前 HEAD：$(git rev-parse --short HEAD)"
    echo "基线 commit：$BASELINE_SHORT ($(git log -1 --format='%s' $BASELINE_COMMIT 2>/dev/null || echo '不可达'))"
    echo
    echo "整改 commit 链（基线之后）："
    git log --oneline "$BASELINE_COMMIT..HEAD" | sed 's/^/  /'
    echo
    local clean="否"
    worktree_clean && clean="是"
    echo "工作区干净：$clean"
    if ! worktree_clean; then
        echo
        echo "未提交改动："
        git status --porcelain | sed 's/^/  /'
    fi
    echo
    log "提示：./scripts/aarch64_refactor_rollback.sh patch  生成回退 patch"
    log "      ./scripts/aarch64_refactor_rollback.sh revert  应用回退（先 dry-run）"
    log "      ./scripts/aarch64_refactor_rollback.sh verify 验证是否回退到位"
}

# -------------------- 子命令：patch --------------------
# 用法：patch [--out PATH]
# 生成 patch 文件，默认输出到仓库外（避免污染工作区）
cmd_patch() {
    check_prerequisites

    local out_path="../aarch64_refactor_full.patch"
    while [ $# -gt 0 ]; do
        case "$1" in
            --out) out_path="$2"; shift 2 ;;
            *) die "patch 子命令未知参数：$1" ;;
        esac
    done

    # 解析为绝对路径
    case "$out_path" in
        /*) : ;;
        *)  out_path="$REPO_ROOT/$out_path" ;;
    esac

    log "生成 patch：基线 $BASELINE_SHORT → HEAD $(git rev-parse --short HEAD)"
    log "输出文件：$out_path"

    # 关键：只 diff 整改涉及的源码路径，避免把 docs/ 等非整改内容卷入
    # docs/architecture/spdiag_aarch64_only_design*.md 是新增文档，回退时不应删
    {
        echo "# aarch64-only refactor rollback patch"
        echo "# Generated: $(date '+%Y-%m-%d %H:%M:%S')"
        echo "# Baseline: $BASELINE_COMMIT"
        echo "# HEAD:     $(git rev-parse HEAD)"
        echo "# Revert with: git apply -R $out_path"
        echo "#"
        git diff "$BASELINE_COMMIT..HEAD" -- "${AFFECTED_PATHS[@]}"
    } > "$out_path"

    local size
    size=$(wc -c < "$out_path")
    log "完成。patch 文件大小：${size} 字节"
    log "回退命令：git apply -R \"$out_path\""
}

# -------------------- 子命令：revert --------------------
# 用法：revert [--apply]
# 默认 dry-run，--apply 才真改
cmd_revert() {
    check_prerequisites

    local apply=0
    while [ $# -gt 0 ]; do
        case "$1" in
            --apply) apply=1; shift ;;
            *) die "revert 子命令未知参数：$1" ;;
        esac
    done

    # 工作区必须干净，避免回退动作与未提交改动冲突导致文件丢失
    if ! worktree_clean; then
        die "工作区不干净。请先 git stash 或 commit 现有改动，避免回退时丢失内容。"
    fi

    # 当前 HEAD 必须在基线之后（否则没东西可回退）
    if [ "$(git rev-parse HEAD)" = "$BASELINE_COMMIT" ]; then
        log "当前 HEAD 已是基线 $BASELINE_SHORT，无需回退。"
        return 0
    fi

    # 生成本地回退 patch（针对当前工作区状态）
    # 注意：tmp_patch 用全局变量，避免 trap 在 set -u 下因作用域问题报错
    TMP_PATCH_FILE="$(mktemp)"
    trap '[ -n "${TMP_PATCH_FILE:-}" ] && [ -f "$TMP_PATCH_FILE" ] && rm -f "$TMP_PATCH_FILE"' EXIT

    git diff "$BASELINE_COMMIT..HEAD" -- "${AFFECTED_PATHS[@]}" > "$TMP_PATCH_FILE"

    if [ ! -s "$TMP_PATCH_FILE" ]; then
        log "整改路径无 diff，无需回退。"
        return 0
    fi

    if [ "$apply" -eq 0 ]; then
        # dry-run：用 git apply --check 验证可应用
        log "=== DRY RUN（不会修改任何文件）==="
        if git apply --check -R "$TMP_PATCH_FILE" 2>/dev/null; then
            log "✓ 回退 patch 可成功应用（dry-run 通过）"
            log "  受影响文件："
            git diff --name-only "$BASELINE_COMMIT..HEAD" -- "${AFFECTED_PATHS[@]}" | sed 's/^/    /'
            log "确认无误后，加 --apply 实际执行："
            log "  ./scripts/aarch64_refactor_rollback.sh revert --apply"
        else
            die "回退 patch 无法干净应用（可能有冲突）。请检查工作区或改用 git revert。"
        fi
        return 0
    fi

    # 真正应用
    log "=== 应用回退 ==="
    log "将以下文件回退到基线 $BASELINE_SHORT："
    git diff --name-only "$BASELINE_COMMIT..HEAD" -- "${AFFECTED_PATHS[@]}" | sed 's/^/  /'

    if ! git apply -R "$TMP_PATCH_FILE"; then
        die "git apply -R 失败。工作区可能已被部分修改，请用 git status 检查。"
    fi

    log "✓ 回退 patch 已应用。文件内容已回到基线，但尚未提交。"
    log "请检查后手动提交："
    log "  git add -A"
    log "  git commit -m \"revert: 一键回退 aarch64-only 整改到基线 $BASELINE_SHORT\""
    log ""
    log "验证回退结果："
    log "  ./scripts/aarch64_refactor_rollback.sh verify"
}

# -------------------- 子命令：verify --------------------
# 验证当前整改路径的文件是否与基线一致
cmd_verify() {
    check_prerequisites

    log "=== 验证回退结果 ==="
    echo
    echo "对比当前工作区 vs 基线 $BASELINE_SHORT（仅整改路径）："
    echo

    local diff_output
    diff_output="$(git diff "$BASELINE_COMMIT" -- "${AFFECTED_PATHS[@]}")"

    if [ -z "$diff_output" ]; then
        log "✓ 整改路径与基线完全一致——回退成功"
        echo
        echo "残留检查（应再次出现非 aarch64 架构宏）："
        local hit=0
        for pattern in '__x86_64__' '__arm__' '_WIN32' 'x86intrin' '__rdtsc' 'SPDIAG_ARCH_X86_64' 'SPDIAG_ARCH_ARM'; do
            local count
            count=$(grep -rln "$pattern" src/ include/ CMakeLists.txt 2>/dev/null | wc -l)
            if [ "$count" -gt 0 ]; then
                echo "  ✓ 出现 '$pattern'：$count 个文件（预期，说明已回退）"
                hit=1
            fi
        done
        [ "$hit" -eq 1 ] || echo "  ⚠ 未发现非 aarch64 宏（可能回退未完成，或基线已部分整改）"
        return 0
    else
        warn "✗ 整改路径与基线不一致——回退未完成或只回退了一部分"
        echo
        echo "不一致的文件："
        git diff --name-only "$BASELINE_COMMIT" -- "${AFFECTED_PATHS[@]}" | sed 's/^/  /'
        echo
        echo "完整 diff："
        echo "$diff_output" | head -50
        return 1
    fi
}

# -------------------- 入口 --------------------
main() {
    [ $# -eq 0 ] && { cmd_status; exit 0; }
    local subcmd="$1"; shift
    case "$subcmd" in
        status)            cmd_status "$@" ;;
        patch)             cmd_patch "$@" ;;
        revert)            cmd_revert "$@" ;;
        verify)            cmd_verify "$@" ;;
        -h|--help|help)
            sed -n '2,20p' "$0"
            exit 0
            ;;
        *)
            die "未知子命令：$subcmd。可用：status | patch | revert | verify"
            ;;
    esac
}

main "$@"
