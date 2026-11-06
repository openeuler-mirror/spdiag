# Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
# SpDiag is licensed under Mulan PSL v2.
cmake_minimum_required(VERSION 3.16)
project(InstalledSpDiagConsumers LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
if(NOT SPDIAG_PACKAGE_DIR)
    message(FATAL_ERROR "SPDIAG_PACKAGE_DIR must select the staged RPM installation")
endif()
find_package(SpDiag CONFIG REQUIRED NO_DEFAULT_PATH PATHS "${SPDIAG_PACKAGE_DIR}")
foreach(mode IN ITEMS shared static)
    add_executable(consumer_${mode} main.cpp)
endforeach()
target_link_libraries(consumer_shared PRIVATE SpDiag::spdiag_lib)
target_link_libraries(consumer_static PRIVATE SpDiag::spdiag_static)
