# Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
# SpDiag is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#         http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.

cmake_minimum_required(VERSION 3.16)

project(SpDiag VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTS "Build tests" ON)
option(BUILD_EXAMPLES "Build examples" ON)
option(ENABLE_PERCENTILE "Enable P99/P999/P9999 percentile calculation" OFF)
option(ENABLE_PERFLOG "Enable PerfLog timestamp logging" OFF)
option(ENABLE_MEMPOINT "Enable point-level memory observation (requires systemtap-sdt-dev)" ON)
option(ENABLE_OB_MEMORY "Enable OB memory allocation tracking via eBPF uprobe (Linux only)" ON)
option(ENABLE_OB_CACHE "Enable OB cachestat: cache-references/cache-misses monitoring via perf_event (Linux only)" ON)
option(SPDIAG_DEBUG_FRAMES "Enable frame resolution debug output" OFF)
option(SPDIAG_BUILD_SHARED "Build spdiag_lib as shared library (.so) instead of static (.a)" OFF)

# spdiag_lib links static helper libraries. They must be compiled as PIC when
# the SDK itself is built as a shared library.
if(SPDIAG_BUILD_SHARED)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

option(SPDIAG_LOG_TO_FILE "Enable log to file" OFF)
option(SPDIAG_LOG_TO_CONSOLE "Enable log to console" ON)
set(SPDIAG_LOG_FILE_PATH "/var/log/spdiag/spdiag.log" CACHE STRING "Log file path")
set(SPDIAG_LOG_MAX_SIZE "10485760" CACHE STRING "Max log file size in bytes")
set(SPDIAG_LOG_MAX_FILES "5" CACHE STRING "Max number of backup log files")
set(SPDIAG_ASYNC_QUEUE_SIZE "8192" CACHE STRING "Async log queue capacity")
# 队列满时业务线程的最长等待时间（毫秒）：
#   > 0 短暂等待后台消费；持续高压下会在业务线程延迟路径上叠加尖刺
#   = 0 立即丢弃（非阻塞模式），完全避免日志路径反噬业务线程
set(SPDIAG_ASYNC_ENQUEUE_TIMEOUT_MS "1" CACHE STRING
    "Async log enqueue timeout on queue full (ms, 0 = non-blocking drop)")
# ERROR 是否绕过异步队列、在调用线程直接同步写入 sinks。
# = 1（默认）：ERROR 保证不因队列满被丢弃、调用返回前即可见；
# = 0：回退到"全部异步"（ERROR 也可能在高压下被丢弃）。
set(SPDIAG_LOG_ERROR_SYNC "1" CACHE STRING
    "Write ERROR logs synchronously in calling thread (1 = default, 0 = fully async)")
# ERROR 同步写入后是否调用 sink->Flush()（flush 到内核页缓存，非 fsync 强制落盘）。
# = 1（默认）：进程崩溃但 OS 存活时通常已落盘；= 0：仅 write 不加 flush。
set(SPDIAG_LOG_ERROR_SYNC_FLUSH "1" CACHE STRING
    "Flush after synchronous ERROR write (1 = default, 0 = write only)")

# ---------------------------------------------------------------------------
# 构建身份信息：生成 _build_info.h，含 git HEAD SHA + 构建时间戳。
# 用于运行时区分二进制来自哪个 commit（spdiag --version 会打印）。
# 验证脚本据此判断 ctest 跑的是否是当前源码编译的 spdiag。
#
# 放在架构硬断言之前：即使架构不匹配 FATAL_ERROR，build_info 也已生成，
# 便于事后排查"哪个 commit 在错架构上 configure 过"。
# ---------------------------------------------------------------------------
find_package(Git QUIET)
set(SPDIAG_GIT_SHA "unknown")
set(SPDIAG_GIT_DIRTY "")
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short=8 HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE _sha OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET RESULT_VARIABLE _rc)
    if(_rc EQUAL 0)
        set(SPDIAG_GIT_SHA "${_sha}")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} status --porcelain
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE _dirty OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(_dirty)
            set(SPDIAG_GIT_DIRTY "-dirty")
        endif()
    endif()
endif()
string(TIMESTAMP SPDIAG_BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%S")
configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/_build_info.h.in
    ${CMAKE_BINARY_DIR}/_build_info.h
    @ONLY)

# ---------------------------------------------------------------------------
# 架构硬断言：SpDiag 仅支持 aarch64。整改后不再做平台/架构分支判定。
# ---------------------------------------------------------------------------
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    message(FATAL_ERROR "SpDiag only supports aarch64/arm64 architecture. Current: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

if(ENABLE_PERCENTILE)
    add_compile_definitions(SPDIAG_ENABLE_PERCENTILE)
endif()

if(ENABLE_PERFLOG)
    add_compile_definitions(SPDIAG_ENABLE_PERFLOG)
endif()

if(ENABLE_MEMPOINT)
    include(CheckIncludeFileCXX)
    set(CMAKE_REQUIRED_INCLUDES_SAVE "${CMAKE_REQUIRED_INCLUDES}")
    list(APPEND CMAKE_REQUIRED_INCLUDES "/usr/include/aarch64-linux-gnu")
    check_include_file_cxx("sys/sdt.h" HAS_SDT_H)
    if(NOT HAS_SDT_H)
        if(EXISTS "/usr/include/aarch64-linux-gnu/sys/sdt.h")
            set(HAS_SDT_H TRUE)
        endif()
    endif()
    set(CMAKE_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES_SAVE}")
    if(NOT HAS_SDT_H)
        message(WARNING "sys/sdt.h not found — install systemtap-sdt-dev to enable MemPoint. Disabling ENABLE_MEMPOINT.")
        set(ENABLE_MEMPOINT OFF CACHE BOOL "Disabled: missing sys/sdt.h" FORCE)
    else()
        add_compile_definitions(SPDIAG_ENABLE_MEMPOINT)
    endif()
endif()

if(ENABLE_OB_MEMORY)
    add_compile_definitions(SPDIAG_ENABLE_OB_MEMORY)
endif()

if(ENABLE_OB_CACHE)
    add_compile_definitions(SPDIAG_ENABLE_OB_CACHE)
endif()

if(ENABLE_OB_MEMORY OR ENABLE_OB_CACHE OR ENABLE_MEMPOINT)
    add_compile_definitions(SPDIAG_HAS_OB)
endif()
if(ENABLE_OB_MEMORY OR ENABLE_MEMPOINT)
    add_compile_definitions(SPDIAG_USES_BPF)
endif()

if(SPDIAG_DEBUG_FRAMES)
    add_compile_definitions(SPDIAG_DEBUG_FRAMES)
endif()

add_compile_options(-Wall -Wextra -Werror)

add_compile_options(-mtune=tsv110 -march=armv8-a)

add_subdirectory(src/logger)
add_subdirectory(src/sdk)
add_subdirectory(src/runtime)
add_subdirectory(src/manager)
add_subdirectory(src/cli)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

include(GNUInstallDirs)

# 安装库文件。共享库已将 logger 私有链接进 libspdiag.so，不需要额外
# 安装 libspdiag_logger.a；静态 SDK 则需要同时导出两个静态库。
if(SPDIAG_BUILD_SHARED)
    install(TARGETS spdiag_lib
        EXPORT SpDiagTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT SpDiagDevelopment
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT SpDiagDevelopment
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            COMPONENT SpDiagDevelopment
    )
else()
    install(TARGETS spdiag_lib spdiag_logger
        EXPORT SpDiagStaticTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT SpDiagStaticDevelopment
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT SpDiagStaticDevelopment
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            COMPONENT SpDiagStaticDevelopment
    )
endif()

# 安装公开头文件
install(DIRECTORY include/spdiag
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

# 导出 CMake Targets（支持 find_package）
if(SPDIAG_BUILD_SHARED)
    install(EXPORT SpDiagTargets
        FILE SpDiagTargets.cmake
        NAMESPACE SpDiag::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SpDiag
        COMPONENT SpDiagDevelopment
    )
else()
    install(EXPORT SpDiagStaticTargets
        FILE SpDiagStaticTargets.cmake
        NAMESPACE SpDiag::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SpDiag
        COMPONENT SpDiagStaticDevelopment
    )
endif()

# 生成并安装 Config 文件
include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/SpDiagConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/SpDiagConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SpDiag
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/SpDiagConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/SpDiagConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/SpDiagConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SpDiag
)

# 安装 CLI 工具
install(TARGETS spdiag
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# 安装默认配置文件
install(FILES config/spdiag.conf.example
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/spdiag
    RENAME spdiag.conf
)

install(DIRECTORY include/spdiag/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/spdiag
    FILES_MATCHING PATTERN "*.h"
)
