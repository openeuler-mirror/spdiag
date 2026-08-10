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

# 单元测试环境设置与运行脚本
# 自动安装 GTest 并构建运行所有测试

set -e

echo "=========================================="
echo "SpDiag 单元测试环境设置"
echo "=========================================="

if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$ID
    echo "检测到操作系统: $OS"
else
    echo "无法检测操作系统"
    OS="unknown"
fi

check_gtest() {
    if pkg-config --exists gtest 2>/dev/null; then
        echo "GTest 已通过 pkg-config 安装"
        return 0
    elif [ -d /usr/include/gtest ]; then
        echo "GTest 头文件已安装"
        return 0
    elif find /usr -name "libgtest*" 2>/dev/null | grep -q .; then
        echo "GTest 库已安装"
        return 0
    fi
    return 1
}

install_gtest() {
    echo ""
    echo "正在安装 GTest..."

    case $OS in
        "centos"|"rhel"|"fedora"|"openEuler")
            echo "使用 dnf 安装..."
            sudo dnf install -y gtest gtest-devel
            ;;
        "ubuntu"|"debian")
            echo "使用 apt-get 安装..."
            sudo apt-get update
            sudo apt-get install -y libgtest-dev cmake build-essential
            ;;
        *)
            echo "未知系统，尝试使用 FetchContent 自动下载..."
            echo "如果网络不可用，请手动安装 GTest 或设置 SPDIAG_GTEST_SOURCE 环境变量"
            return 0
            ;;
    esac

    echo "GTest 安装完成"
}

if ! check_gtest; then
    echo ""
    echo "未检测到 GTest，开始安装..."
    install_gtest
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_DIR"

echo ""
echo "使用 build.sh 构建并运行测试..."
exec ./build.sh --clean --test-only "$@"
