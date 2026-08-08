# Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
# SpDiag is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#         http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.

add_executable(demo_app
    demo_app.cpp
)

target_include_directories(demo_app PRIVATE "${CMAKE_SOURCE_DIR}/include")

target_link_libraries(demo_app PRIVATE spdiag_lib)

if(NOT WIN32)
    target_link_options(demo_app PRIVATE -rdynamic)
endif()

add_executable(csv_export_demo
    csv_export_demo.cpp
)

target_include_directories(csv_export_demo PRIVATE "${CMAKE_SOURCE_DIR}/include")

target_link_libraries(csv_export_demo PRIVATE spdiag_lib)

if(NOT WIN32)
    target_link_options(csv_export_demo PRIVATE -rdynamic)
endif()

add_executable(demo_app_unbound
    demo_app_unbound.cpp
)

target_include_directories(demo_app_unbound PRIVATE "${CMAKE_SOURCE_DIR}/include")

target_link_libraries(demo_app_unbound PRIVATE spdiag_lib)

add_executable(benchmark_time
    benchmark_time.cpp
)

target_include_directories(benchmark_time PRIVATE "${CMAKE_SOURCE_DIR}/include")

target_link_libraries(benchmark_time PRIVATE spdiag_lib)

add_executable(benchmark_memory
    benchmark_memory.cpp
)

target_include_directories(benchmark_memory PRIVATE "${CMAKE_SOURCE_DIR}/include")

target_link_libraries(benchmark_memory PRIVATE spdiag_lib)

add_executable(benchmark_io
    benchmark_io.cpp
)

target_include_directories(benchmark_io PRIVATE "${CMAKE_SOURCE_DIR}/include")

target_link_libraries(benchmark_io PRIVATE spdiag_lib)

add_executable(demo_app_concurrent
    demo_app_concurrent.cpp
)

target_include_directories(demo_app_concurrent PRIVATE "${CMAKE_SOURCE_DIR}/include")

target_link_libraries(demo_app_concurrent PRIVATE spdiag_lib)

add_executable(demo_global_perfpoint
    demo_global_perfpoint.cpp
)

target_include_directories(demo_global_perfpoint PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/src")

target_link_libraries(demo_global_perfpoint PRIVATE spdiag_lib)

add_executable(benchmark_perfpoint_overhead
    benchmark_perfpoint_overhead.cpp
)

target_include_directories(benchmark_perfpoint_overhead PRIVATE "${CMAKE_SOURCE_DIR}/include")

target_link_libraries(benchmark_perfpoint_overhead PRIVATE spdiag_lib)

add_executable(benchmark_start_stop_overhead
    benchmark_start_stop_overhead.cpp
)

target_include_directories(benchmark_start_stop_overhead PRIVATE "${CMAKE_SOURCE_DIR}/include")

target_link_libraries(benchmark_start_stop_overhead PRIVATE spdiag_lib)

add_executable(test_conflict
    test_conflict.cpp
)

target_include_directories(test_conflict PRIVATE "${CMAKE_SOURCE_DIR}/include")

target_link_libraries(test_conflict PRIVATE spdiag_lib)

add_executable(demo_app_concurrent_init_per_loop
    demo_app_concurrent_init_per_loop.cpp
)

target_include_directories(demo_app_concurrent_init_per_loop PRIVATE "${CMAKE_SOURCE_DIR}/include")

target_link_libraries(demo_app_concurrent_init_per_loop PRIVATE spdiag_lib)

# --- OB feature demos (conditional) ---

# 临时验证脚本（验证完成后删除）
add_executable(test_watcher
    test_watcher.cpp
)
target_include_directories(test_watcher PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(test_watcher PRIVATE spdiag_lib)

add_executable(test_watcher_auto
    test_watcher_auto.cpp
)
target_include_directories(test_watcher_auto PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(test_watcher_auto PRIVATE spdiag_lib)

if(ENABLE_OB_MEMORY)
    add_executable(memstat_demo
        memstat_demo.cpp
    )

    if(NOT WIN32)
        target_link_options(memstat_demo PRIVATE -rdynamic)
    endif()
endif()

if(ENABLE_OB_CACHE)
    add_executable(cachestat_demo
        cachestat_demo.cpp
    )

    if(NOT WIN32)
        target_link_options(cachestat_demo PRIVATE -rdynamic)
    endif()
endif()

if(ENABLE_MEMPOINT)
    add_executable(mempoint_demo
        mempoint_demo.cpp
    )

    target_include_directories(mempoint_demo PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_link_libraries(mempoint_demo PRIVATE spdiag_lib)

    if(NOT WIN32)
        target_link_options(mempoint_demo PRIVATE -rdynamic)
    endif()
endif()

# --- Logger demo ---

add_executable(logger_demo
    logger_demo.cpp
)

target_include_directories(logger_demo PRIVATE "${CMAKE_SOURCE_DIR}/include")

target_link_libraries(logger_demo PRIVATE spdiag_lib)

if(NOT WIN32)
    target_link_options(logger_demo PRIVATE -rdynamic)
endif()
