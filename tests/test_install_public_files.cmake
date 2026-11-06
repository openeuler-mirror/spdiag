# Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
# SpDiag is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#         http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.

if(NOT DEFINED SPDIAG_BUILD_DIR OR NOT DEFINED SPDIAG_INSTALL_DIR)
    message(FATAL_ERROR "SPDIAG_BUILD_DIR and SPDIAG_INSTALL_DIR are required")
endif()

file(REMOVE_RECURSE "${SPDIAG_INSTALL_DIR}")
# DESTDIR also contains absolute install destinations such as /etc in RPM builds.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "DESTDIR=${SPDIAG_INSTALL_DIR}"
            "${CMAKE_COMMAND}" --install "${SPDIAG_BUILD_DIR}" --prefix /
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_output
    ERROR_VARIABLE install_error
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR
        "cmake --install failed (${install_result})\n${install_output}\n${install_error}")
endif()

foreach(public_file IN ITEMS perf_key_defined.h perf_point.def)
    if(NOT EXISTS "${SPDIAG_INSTALL_DIR}/include/spdiag/${public_file}")
        message(FATAL_ERROR "missing installed public file: ${public_file}")
    endif()
endforeach()

set(installed_config "${SPDIAG_INSTALL_DIR}/etc/spdiag/spdiag.conf")
if(NOT EXISTS "${installed_config}")
    message(FATAL_ERROR "missing installed default config: ${installed_config}")
endif()
file(READ "${installed_config}" config_content)
foreach(expected_line IN ITEMS "maxSlots = 2048" "maxPrograms = 8")
    string(FIND "${config_content}" "${expected_line}" expected_index)
    if(expected_index EQUAL -1)
        message(FATAL_ERROR
            "installed default config is missing: ${expected_line}")
    endif()
endforeach()
