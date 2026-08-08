# Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
# SpDiag is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#         http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.

# BPF compilation only needed for memstat (uprobe) and mempoint (USDT)
if(ENABLE_OB_MEMORY OR ENABLE_MEMPOINT)
    find_program(SPDIAG_CLANG clang)
    find_program(SPDIAG_BPFTOOL bpftool)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBBPF REQUIRED libbpf>=1.0)

    set(LIBBPF_BPF_INCLUDE_ARGS ${LIBBPF_INCLUDE_DIRS})
    list(TRANSFORM LIBBPF_BPF_INCLUDE_ARGS PREPEND "-I")
    target_include_directories(spdiag_runtime_lib PRIVATE ${LIBBPF_INCLUDE_DIRS})

    if(SPDIAG_CLANG)
        set(BPF_SRC "${CMAKE_CURRENT_SOURCE_DIR}/bpf/ob_point.bpf.c")
        set(BPF_OBJ "${CMAKE_CURRENT_BINARY_DIR}/ob_point.bpf.o")
        set(SKEL_HEADER "${CMAKE_CURRENT_BINARY_DIR}/ob_point.skel.h")

        add_custom_command(
            OUTPUT ${BPF_OBJ}
            COMMAND ${SPDIAG_CLANG} -target bpf -g -O2
                -D__TARGET_ARCH_arm64
                -I${CMAKE_CURRENT_SOURCE_DIR}/bpf
                ${LIBBPF_BPF_INCLUDE_ARGS}
                -c ${BPF_SRC} -o ${BPF_OBJ}
            DEPENDS ${BPF_SRC}
            COMMENT "Compiling eBPF object ob_point.bpf.o"
            VERBATIM
        )

        if(SPDIAG_BPFTOOL)
            add_custom_command(
                OUTPUT ${SKEL_HEADER}
                COMMAND ${SPDIAG_BPFTOOL} gen skeleton ${BPF_OBJ} > ${SKEL_HEADER}
                DEPENDS ${BPF_OBJ}
                COMMENT "Generating BPF skeleton ob_point.skel.h"
                VERBATIM
            )
        else()
            file(WRITE ${SKEL_HEADER} "")
        endif()

        add_custom_target(spdiag_ob_bpf ALL DEPENDS ${BPF_OBJ} ${SKEL_HEADER})
        add_dependencies(spdiag_runtime_lib spdiag_ob_bpf)
        if(TARGET spdiag_bpf_loader)
            add_dependencies(spdiag_bpf_loader spdiag_ob_bpf)
        endif()

        target_include_directories(spdiag_runtime_lib PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
        if(TARGET spdiag_bpf_loader)
            target_include_directories(spdiag_bpf_loader PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
        endif()
    endif()
endif()
