/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
 * SpDiag is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *         http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#pragma once

//
// platform.h — 公共头文件，aarch64-only 整改后仅保留骨架。
//
// 历史上本文件定义 SPDIAG_ARCH_AARCH64/X86_64/ARM 三组架构宏，供 SDK 内部
// 区分架构。整改后 SpDiag 仅支持 aarch64，CMake 不再注入这些宏，源码也直接
// 使用 __aarch64__ 内置宏。本文件保留为空骨架以维持 include 路径与下游
// find_package 安装头文件集合不变。
//
// 真正的架构适配入口是 src/platform/linux/platform_linux.cpp，参见其顶部
// 的"架构适配层契约"注释。
//
