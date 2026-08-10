# Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
# SpDiag is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#         http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

if(DEFINED ENV{SPDIAG_GTEST_SOURCE})
    message(STATUS "Using GTest from SPDIAG_GTEST_SOURCE: $ENV{SPDIAG_GTEST_SOURCE}")
    add_subdirectory($ENV{SPDIAG_GTEST_SOURCE} googletest-build)
    set(SPDIAG_GTEST_TARGETS gtest gtest_main)
else()
    find_package(GTest QUIET)
    if(GTest_FOUND)
        message(STATUS "Found system GTest: ${GTest_DIR}")
        set(SPDIAG_GTEST_TARGETS GTest::gtest GTest::gtest_main)
    else()
        message(STATUS "GTest not found, downloading via FetchContent...")
        include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG        v1.14.0
        )
        FetchContent_MakeAvailable(googletest)
        set(SPDIAG_GTEST_TARGETS gtest gtest_main)
    endif()
endif()

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")
list(FILTER TEST_SOURCES EXCLUDE REGEX "test_dump_conflict_")
list(FILTER TEST_SOURCES EXCLUDE REGEX "test_perf_point_disabled\\.cpp$")

set(SPDIAG_CLI_DISPLAY_TEST_SOURCES
    "${CMAKE_SOURCE_DIR}/src/cli/display_engine.cpp"
    "${CMAKE_SOURCE_DIR}/src/cli/memstat_display.cpp"
    "${CMAKE_SOURCE_DIR}/src/cli/mempoint_display.cpp"
    "${CMAKE_SOURCE_DIR}/src/cli/cache_display.cpp"
    "${CMAKE_SOURCE_DIR}/src/cli/sort_spec.cpp"
    "${CMAKE_SOURCE_DIR}/src/cli/csv_writer.cpp")

foreach(TEST_FILE ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_FILE})
    target_include_directories(${TEST_NAME} PRIVATE
        "${CMAKE_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/src/cli"
    )
    target_link_libraries(${TEST_NAME} PRIVATE
        spdiag_lib spdiag_runtime_lib spdiag_manager_lib ${SPDIAG_GTEST_TARGETS})

    if(TEST_NAME STREQUAL "test_logger" OR TEST_NAME STREQUAL "test_signal_watcher")
        # 设置 SPDIAG_LOG_LEVEL=0 以放开日志级别过滤，确保测试中所有级别
        # 的日志消息（包括 DEBUG/INFO/WARN/ERROR）都能到达 CollectingSink。
        target_compile_definitions(${TEST_NAME} PRIVATE SPDIAG_LOG_LEVEL=0)
    endif()

    if(TEST_NAME STREQUAL "test_signal_watcher")
        target_link_libraries(${TEST_NAME} PRIVATE spdiag_logger)
    endif()

    if(TEST_NAME STREQUAL "test_cli_config")
        target_sources(${TEST_NAME} PRIVATE
            "${CMAKE_SOURCE_DIR}/src/cli/cli_config.cpp"
            "${CMAKE_SOURCE_DIR}/src/cli/sort_spec.cpp")
        target_include_directories(${TEST_NAME} PRIVATE "${CMAKE_BINARY_DIR}")
    endif()

    if(TEST_NAME STREQUAL "test_cli_config_extra")
        target_sources(${TEST_NAME} PRIVATE
            "${CMAKE_SOURCE_DIR}/src/cli/cli_config.cpp"
            "${CMAKE_SOURCE_DIR}/src/cli/sort_spec.cpp")
        target_include_directories(${TEST_NAME} PRIVATE "${CMAKE_BINARY_DIR}")
    endif()

    if(TEST_NAME STREQUAL "test_sort_engine")
        target_sources(${TEST_NAME} PRIVATE
            "${CMAKE_SOURCE_DIR}/src/cli/sort_spec.cpp")
    endif()

    if(TEST_NAME STREQUAL "test_display_engine" OR TEST_NAME STREQUAL "test_display_engine_cache")
        target_sources(${TEST_NAME} PRIVATE ${SPDIAG_CLI_DISPLAY_TEST_SOURCES})
    endif()

    if(TEST_NAME STREQUAL "test_csv_writer")
        target_sources(${TEST_NAME} PRIVATE
            "${CMAKE_SOURCE_DIR}/src/cli/csv_writer.cpp")
    endif()

    if(TEST_NAME STREQUAL "test_cli_mempoint")
        target_sources(${TEST_NAME} PRIVATE
            ${SPDIAG_CLI_DISPLAY_TEST_SOURCES}
            "${CMAKE_SOURCE_DIR}/src/cli/cli_config.cpp")
        target_include_directories(${TEST_NAME} PRIVATE "${CMAKE_BINARY_DIR}")
    endif()

    if(TEST_NAME STREQUAL "test_mem_point_sdk"
       OR TEST_NAME STREQUAL "test_object_mem_point")
        target_compile_definitions(${TEST_NAME} PRIVATE SPDIAG_TEST)
    endif()

    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()

# The disabled PerfPoint must retain the normal source syntax without pulling
# in any SDK implementation or link dependency.
add_executable(test_perf_point_disabled test_perf_point_disabled.cpp)
target_include_directories(test_perf_point_disabled PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
)
target_compile_definitions(test_perf_point_disabled PRIVATE SPDIAG_DISABLE)
add_test(NAME test_perf_point_disabled COMMAND test_perf_point_disabled)

# ─── dump conflict test targets (standalone, no GTest) ───

add_executable(test_dump_conflict_worker test_dump_conflict_worker.cpp)
target_include_directories(test_dump_conflict_worker PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/src/sdk"
)
target_link_libraries(test_dump_conflict_worker PRIVATE spdiag_lib)

add_executable(test_dump_conflict_reader test_dump_conflict_reader.cpp)
target_include_directories(test_dump_conflict_reader PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/src/sdk"
)
target_link_libraries(test_dump_conflict_reader PRIVATE spdiag_lib)

# user_def_test validates an already installed SDK and is built separately by
# build.sh after installation. Register it only when that binary exists so
# normal CTest/RPM builds remain self-contained.
set(SPDIAG_USER_DEF_TEST "${CMAKE_SOURCE_DIR}/tests/user_def_test/build/user_def_test")
if(EXISTS "${SPDIAG_USER_DEF_TEST}")
    add_test(NAME user_def_test COMMAND "${SPDIAG_USER_DEF_TEST}")
endif()
