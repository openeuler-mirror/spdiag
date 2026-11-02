# Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
# SpDiag is licensed under Mulan PSL v2.

set(SPDIAG_SOURCE_COMMIT "" CACHE STRING "Full source commit supplied by the packager")
set(SPDIAG_GIT_SHA "unknown")
set(SPDIAG_GIT_DIRTY "")
set(_source_commit "${SPDIAG_SOURCE_COMMIT}")
if(NOT _source_commit AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE _source_commit OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            set(_source_commit "")
        else()
            execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                OUTPUT_VARIABLE _dirty OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(_dirty)
                set(SPDIAG_GIT_DIRTY "-dirty")
            endif()
        endif()
    endif()
endif()
if(NOT _source_commit AND EXISTS "${CMAKE_SOURCE_DIR}/cmake/source_revision.txt")
    file(READ "${CMAKE_SOURCE_DIR}/cmake/source_revision.txt" _archive_commit)
    string(STRIP "${_archive_commit}" _archive_commit)
    # Ignore the unexpanded export-subst placeholder in a Git checkout.
    if(NOT _archive_commit MATCHES "^\\$Format:")
        set(_source_commit "${_archive_commit}")
    endif()
endif()
if(_source_commit)
    string(LENGTH "${_source_commit}" _commit_length)
    if(NOT _source_commit MATCHES "^[0-9a-f]+$" OR
       NOT (_commit_length EQUAL 40 OR _commit_length EQUAL 64))
        message(FATAL_ERROR "Invalid full source commit: ${_source_commit}")
    endif()
    set(SPDIAG_GIT_SHA "${_source_commit}")
endif()
