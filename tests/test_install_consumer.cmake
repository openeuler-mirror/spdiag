# Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
# SpDiag is licensed under Mulan PSL v2.

# Use a separate consumer project and only installed targets/headers.
set(stage "${SPDIAG_BUILD_DIR}/test_install_consumer/stage")
set(consumer "${SPDIAG_BUILD_DIR}/test_install_consumer/consumer")
function(run_checked)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE result
        OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${ARGV}\n${output}\n${error}")
    endif()
endfunction()
# This directory belongs exclusively to this test; remove stale installations.
file(REMOVE_RECURSE "${SPDIAG_BUILD_DIR}/test_install_consumer")
# DESTDIR also contains absolute install destinations such as /etc and libdir.
run_checked("${CMAKE_COMMAND}" -E env "DESTDIR=${stage}" "${CMAKE_COMMAND}"
    --install "${SPDIAG_BUILD_DIR}")
file(GLOB_RECURSE configs "${stage}/*SpDiagConfig.cmake")
list(LENGTH configs config_count)
if(NOT config_count EQUAL 1)
    message(FATAL_ERROR "Expected exactly one installed SpDiagConfig.cmake: ${configs}")
endif()
list(GET configs 0 config)
get_filename_component(package_dir "${config}" DIRECTORY)
file(MAKE_DIRECTORY "${consumer}")
file(WRITE "${consumer}/main.cpp" "#include <spdiag/perf.h>\nint main() { SpDiag::PerfPoint p(SpDiag::PerfKeyDefined::COMPUTE_MATMUL); p.Start(); p.End(0); }\n")
if(SPDIAG_BUILD_SHARED)
    set(target "SpDiag::spdiag_lib")
else()
    set(target "SpDiag::spdiag_static")
endif()
file(WRITE "${consumer}/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.16)\nproject(Consumer LANGUAGES CXX)\nfind_package(SpDiag CONFIG REQUIRED NO_DEFAULT_PATH PATHS \"${package_dir}\")\nadd_executable(consumer main.cpp)\nset_property(TARGET consumer PROPERTY CXX_STANDARD 17)\ntarget_link_libraries(consumer PRIVATE ${target})\n")
set(args "-DCMAKE_CXX_COMPILER=${SPDIAG_CXX_COMPILER}"
    -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF)
if(SPDIAG_TOOLCHAIN_FILE)
    list(APPEND args "-DCMAKE_TOOLCHAIN_FILE=${SPDIAG_TOOLCHAIN_FILE}")
endif()
run_checked("${CMAKE_COMMAND}" -S "${consumer}" -B "${consumer}/build" ${args})
run_checked("${CMAKE_COMMAND}" --build "${consumer}/build")
