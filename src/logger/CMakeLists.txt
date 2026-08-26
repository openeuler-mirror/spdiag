# Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
# SpDiag is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#         http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.

find_package(Threads REQUIRED)

file(GLOB LOGGER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(spdiag_logger STATIC ${LOGGER_SOURCES})

target_include_directories(spdiag_logger PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(spdiag_logger PUBLIC Threads::Threads)

# 以下编译定义均使用 PRIVATE：仅 spdiag_logger 自己编译时生效。
# 其它 target 通过 include "spdiag/log_config.h" 的 #ifndef 守护拿到相同默认值，
# 避免下游 target 想覆盖配置时出现宏重定义冲突。

if(SPDIAG_LOG_TO_FILE)
    target_compile_definitions(spdiag_logger PRIVATE
        SPDIAG_LOG_TO_FILE=1
        SPDIAG_LOG_FILE_PATH="${SPDIAG_LOG_FILE_PATH}"
        SPDIAG_LOG_MAX_SIZE=${SPDIAG_LOG_MAX_SIZE}
        SPDIAG_LOG_MAX_FILES=${SPDIAG_LOG_MAX_FILES}
    )
else()
    target_compile_definitions(spdiag_logger PRIVATE
        SPDIAG_LOG_TO_FILE=0
    )
endif()

if(SPDIAG_LOG_TO_CONSOLE)
    target_compile_definitions(spdiag_logger PRIVATE
        SPDIAG_LOG_TO_CONSOLE=1
    )
else()
    target_compile_definitions(spdiag_logger PRIVATE
        SPDIAG_LOG_TO_CONSOLE=0
    )
endif()

target_compile_definitions(spdiag_logger PRIVATE
    SPDIAG_ASYNC_QUEUE_SIZE=${SPDIAG_ASYNC_QUEUE_SIZE}
    SPDIAG_ASYNC_ENQUEUE_TIMEOUT_MS=${SPDIAG_ASYNC_ENQUEUE_TIMEOUT_MS}
    SPDIAG_LOG_ERROR_SYNC=${SPDIAG_LOG_ERROR_SYNC}
    SPDIAG_LOG_ERROR_SYNC_FLUSH=${SPDIAG_LOG_ERROR_SYNC_FLUSH}
)
