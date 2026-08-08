# Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
# SpDiag is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#         http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.

file(GLOB RUNTIME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/custom_perfpoint/*.cpp"
)

# Only include ob_point sources when any OB feature is enabled
if(ENABLE_OB_MEMORY OR ENABLE_OB_CACHE OR ENABLE_MEMPOINT)
    file(GLOB OB_POINT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/plugins/ob_point/*.cpp"
    )
    list(APPEND RUNTIME_SOURCES ${OB_POINT_SOURCES})
endif()

# Each OB observation plugin is its own directory, compiled only under its
# specific feature flag (mirrors the SPDIAG_ENABLE_OB_* compile definitions).
if(ENABLE_OB_MEMORY)
    file(GLOB MEMSTAT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/plugins/memstat/*.cpp")
    list(APPEND RUNTIME_SOURCES ${MEMSTAT_SOURCES})
endif()
if(ENABLE_OB_CACHE)
    file(GLOB CACHESTAT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/plugins/cachestat/*.cpp")
    list(APPEND RUNTIME_SOURCES ${CACHESTAT_SOURCES})
endif()
if(ENABLE_MEMPOINT)
    file(GLOB MEMPOINT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/plugins/mempoint/*.cpp")
    list(APPEND RUNTIME_SOURCES ${MEMPOINT_SOURCES})
endif()

add_library(spdiag_runtime_lib STATIC ${RUNTIME_SOURCES})

target_include_directories(spdiag_runtime_lib
    PUBLIC
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(ENABLE_OB_MEMORY OR ENABLE_OB_CACHE OR ENABLE_MEMPOINT)
    target_include_directories(spdiag_runtime_lib
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/plugins/ob_point"
    )
endif()

target_link_libraries(spdiag_runtime_lib
    PUBLIC
        spdiag_lib
)

if(ENABLE_OB_MEMORY OR ENABLE_MEMPOINT)
    add_subdirectory(ebpf)
endif()
if(ENABLE_OB_MEMORY OR ENABLE_OB_CACHE OR ENABLE_MEMPOINT)
    add_subdirectory(plugins/ob_point)
endif()
if(ENABLE_OB_MEMORY OR ENABLE_MEMPOINT)
    target_link_libraries(spdiag_runtime_lib PUBLIC spdiag_bpf_loader)
endif()
