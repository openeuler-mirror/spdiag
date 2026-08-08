# Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
# SpDiag is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#         http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.

file(GLOB BPF_LOADER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBBPF REQUIRED libbpf>=1.0)
pkg_check_modules(LIBELF REQUIRED libelf)
find_package(ZLIB REQUIRED)

set(LIBBPF_BPF_INCLUDE_ARGS ${LIBBPF_INCLUDE_DIRS})
list(TRANSFORM LIBBPF_BPF_INCLUDE_ARGS PREPEND "-I")

# BPF skeleton 自动生成
find_program(BPFTOOL NAMES bpftool REQUIRED)
find_program(CLANG NAMES clang REQUIRED)

set(BPF_C_SRC "${CMAKE_SOURCE_DIR}/src/runtime/plugins/ob_point/bpf/ob_point.bpf.c")
set(BPF_OBJ     "${CMAKE_CURRENT_BINARY_DIR}/ob_point.bpf.o")
set(BPF_SKEL_H  "${CMAKE_CURRENT_BINARY_DIR}/ob_point.skel.h")

# 整改后固定 arm64，不再做宿主架构推导
set(BPF_TARGET_ARCH "-D__TARGET_ARCH_arm64")

# 1) clang -target bpf 编译 BPF C → .o
add_custom_command(
    OUTPUT ${BPF_OBJ}
    COMMAND ${CLANG} -target bpf -O2 -g ${BPF_TARGET_ARCH}
            ${LIBBPF_BPF_INCLUDE_ARGS}
            -c ${BPF_C_SRC} -o ${BPF_OBJ}
    DEPENDS ${BPF_C_SRC}
    COMMENT "Compiling BPF program: ob_point.bpf.c"
)

# 2) bpftool gen skeleton 生成 .skel.h
add_custom_command(
    OUTPUT ${BPF_SKEL_H}
    COMMAND ${BPFTOOL} gen skeleton ${BPF_OBJ} > ${BPF_SKEL_H}
    DEPENDS ${BPF_OBJ}
    COMMENT "Generating BPF skeleton: ob_point.skel.h"
)

# 3) 创建虚拟 target 驱动生成链
add_custom_target(spdiag_bpf_skeleton DEPENDS ${BPF_SKEL_H})

add_library(spdiag_bpf_loader STATIC ${BPF_LOADER_SOURCES})

# spdiag_bpf_loader 依赖 skeleton 先生成
add_dependencies(spdiag_bpf_loader spdiag_bpf_skeleton)

target_include_directories(spdiag_bpf_loader
    PUBLIC
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/src"
    PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}"   # 让 bpf_loader.cpp 找到 ob_point.skel.h
        ${LIBBPF_INCLUDE_DIRS}
        ${LIBELF_INCLUDE_DIRS}
)
target_link_directories(spdiag_bpf_loader PUBLIC
    ${LIBBPF_LIBRARY_DIRS}
    ${LIBELF_LIBRARY_DIRS}
)
target_link_libraries(spdiag_bpf_loader PUBLIC ${LIBBPF_LIBRARIES} ${LIBELF_LIBRARIES} ZLIB::ZLIB)
