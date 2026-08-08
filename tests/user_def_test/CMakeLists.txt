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
project(UserDefTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Werror)

find_package(SpDiag REQUIRED)

add_executable(user_def_test main.cpp)
target_include_directories(user_def_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(user_def_test PRIVATE SpDiag::spdiag_lib)

# Compile-time disabled integration test. It deliberately keeps the normal
# PerfPoint call syntax and links the installed package target just like a
# user application; the optimized binary must not reference the SDK.
add_executable(user_def_test_disabled disabled_main.cpp)
target_include_directories(user_def_test_disabled PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(user_def_test_disabled PRIVATE SPDIAG_DISABLE)
target_link_libraries(user_def_test_disabled PRIVATE SpDiag::spdiag_lib)
