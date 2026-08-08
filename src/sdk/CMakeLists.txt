# Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
# SpDiag is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#         http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.

file(GLOB SDK_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../platform/linux/*.cpp")

if(SPDIAG_BUILD_SHARED)
    add_library(spdiag_lib SHARED ${SDK_SOURCES} ${PLATFORM_SOURCES})
else()
    add_library(spdiag_lib STATIC ${SDK_SOURCES} ${PLATFORM_SOURCES})
endif()

set_target_properties(spdiag_lib PROPERTIES OUTPUT_NAME spdiag)
if(SPDIAG_BUILD_SHARED)
    set_target_properties(spdiag_lib PROPERTIES
        EXPORT_NAME spdiag_lib
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )
else()
    # Keep the build-tree target name stable while exporting a distinct
    # installed target that can coexist with the shared SDK.
    set_target_properties(spdiag_lib PROPERTIES
        EXPORT_NAME spdiag_static
    )
endif()

# PUBLIC compile definitions - propagate to consumers via find_package
if(ENABLE_PERCENTILE)
    target_compile_definitions(spdiag_lib PUBLIC SPDIAG_ENABLE_PERCENTILE)
endif()
if(ENABLE_PERFLOG)
    target_compile_definitions(spdiag_lib PUBLIC SPDIAG_ENABLE_PERFLOG)
endif()
if(ENABLE_MEMPOINT)
    target_compile_definitions(spdiag_lib PUBLIC SPDIAG_ENABLE_MEMPOINT)
endif()
if(ENABLE_OB_MEMORY)
    target_compile_definitions(spdiag_lib PUBLIC SPDIAG_ENABLE_OB_MEMORY)
endif()
if(ENABLE_OB_CACHE)
    target_compile_definitions(spdiag_lib PUBLIC SPDIAG_ENABLE_OB_CACHE)
endif()
if(SPDIAG_DEBUG_FRAMES)
    target_compile_definitions(spdiag_lib PUBLIC SPDIAG_DEBUG_FRAMES)
endif()

target_compile_options(spdiag_lib PUBLIC -Wno-stringop-overflow)

target_include_directories(spdiag_lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

if(SPDIAG_BUILD_SHARED)
    target_link_libraries(spdiag_lib PRIVATE spdiag_logger)
else()
    target_link_libraries(spdiag_lib PUBLIC spdiag_logger)
endif()

target_link_libraries(spdiag_lib PUBLIC pthread rt)
