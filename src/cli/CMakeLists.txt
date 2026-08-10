# Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
# SpDiag is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#         http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.

set(CLI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/cli_config.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/csv_writer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/display_engine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sort_spec.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/memstat_display.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/mempoint_display.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/cache_display.cpp"
)

add_executable(spdiag ${CLI_SOURCES})

target_include_directories(spdiag PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_BINARY_DIR}")   # 含 CMake 生成的 _build_info.h

target_link_libraries(spdiag PRIVATE spdiag_manager_lib spdiag_logger)
