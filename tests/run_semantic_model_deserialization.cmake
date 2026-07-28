cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED APISIG_EXE)
    message(FATAL_ERROR "APISIG_EXE is required")
endif()
if(NOT DEFINED FIXTURE_DIR)
    message(FATAL_ERROR "FIXTURE_DIR is required")
endif()
if(NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "WORK_DIR is required")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
file(TO_CMAKE_PATH "${FIXTURE_DIR}" FIXTURE_DIR_NORM)
file(TO_CMAKE_PATH "${WORK_DIR}" WORK_DIR_NORM)

set(TU "${FIXTURE_DIR_NORM}/tu_transitive.cpp")
set(COMPDB "${WORK_DIR_NORM}/compile_commands.json")
set(AST_REPORT "${WORK_DIR_NORM}/ast-report.json")
set(BAD_SCHEMA_REPORT "${WORK_DIR_NORM}/ast-report.bad-schema.json")
set(BAD_VERSION_REPORT "${WORK_DIR_NORM}/ast-report.bad-version.json")

file(WRITE "${COMPDB}" "[
  {
    \"directory\": \"${FIXTURE_DIR_NORM}\",
    \"file\": \"${TU}\",
    \"arguments\": [\"clang++\", \"-std=c++20\", \"-I${FIXTURE_DIR_NORM}\", \"-c\", \"${TU}\"]
  }
]
")

execute_process(
    COMMAND "${APISIG_EXE}" extract --compdb "${COMPDB}" --source-root "${FIXTURE_DIR_NORM}" --no-tooling-banner --ast-report-out "${AST_REPORT}" --json
    RESULT_VARIABLE extract_exit_code
    OUTPUT_VARIABLE extract_output
    ERROR_VARIABLE extract_error)

if(NOT extract_exit_code EQUAL 0)
    message(FATAL_ERROR "extract failed\nstdout:\n${extract_output}\nstderr:\n${extract_error}")
endif()

execute_process(
    COMMAND "${APISIG_EXE}" compute --compdb "${COMPDB}" --source-root "${FIXTURE_DIR_NORM}" --no-tooling-banner --json
    RESULT_VARIABLE compdb_exit_code
    OUTPUT_VARIABLE compdb_output
    ERROR_VARIABLE compdb_error)
if(NOT compdb_exit_code EQUAL 0)
    message(FATAL_ERROR "compute(compdb) failed\nstdout:\n${compdb_output}\nstderr:\n${compdb_error}")
endif()
string(JSON compdb_hash GET "${compdb_output}" api_hash)

execute_process(
    COMMAND "${APISIG_EXE}" compute --ast-report-json "${AST_REPORT}" --json
    RESULT_VARIABLE semantic_exit_code
    OUTPUT_VARIABLE semantic_output
    ERROR_VARIABLE semantic_error)
if(NOT semantic_exit_code EQUAL 0)
    message(FATAL_ERROR "compute(ast-report-json) failed\nstdout:\n${semantic_output}\nstderr:\n${semantic_error}")
endif()
string(JSON semantic_hash GET "${semantic_output}" api_hash)

if(NOT compdb_hash STREQUAL semantic_hash)
    message(FATAL_ERROR "Expected same api_hash from compdb and ast-report-json, got ${compdb_hash} vs ${semantic_hash}")
endif()

file(READ "${AST_REPORT}" report_text)
string(REPLACE "\"schema\": \"apisig.ast-report.v1\"" "\"schema\": \"apisig.ast-report.v0\"" bad_schema_text "${report_text}")
file(WRITE "${BAD_SCHEMA_REPORT}" "${bad_schema_text}")

execute_process(
    COMMAND "${APISIG_EXE}" compute --ast-report-json "${BAD_SCHEMA_REPORT}" --json
    RESULT_VARIABLE bad_schema_exit_code
    OUTPUT_VARIABLE bad_schema_output
    ERROR_VARIABLE bad_schema_error)
if(NOT bad_schema_exit_code EQUAL 1)
    message(FATAL_ERROR "Expected bad schema report to fail with exit code 1, got ${bad_schema_exit_code}\nstdout:\n${bad_schema_output}\nstderr:\n${bad_schema_error}")
endif()

string(REPLACE "\"format_version\": 1" "\"format_version\": 2" bad_version_text "${report_text}")
file(WRITE "${BAD_VERSION_REPORT}" "${bad_version_text}")

execute_process(
    COMMAND "${APISIG_EXE}" compute --ast-report-json "${BAD_VERSION_REPORT}" --json
    RESULT_VARIABLE bad_version_exit_code
    OUTPUT_VARIABLE bad_version_output
    ERROR_VARIABLE bad_version_error)
if(NOT bad_version_exit_code EQUAL 1)
    message(FATAL_ERROR "Expected bad version report to fail with exit code 1, got ${bad_version_exit_code}\nstdout:\n${bad_version_output}\nstderr:\n${bad_version_error}")
endif()

message(STATUS "semantic model deserialization and validation checks passed")
