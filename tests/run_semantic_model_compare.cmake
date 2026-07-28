cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED APISIG_EXE)
    message(FATAL_ERROR "APISIG_EXE is required")
endif()
if(NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "WORK_DIR is required")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
file(TO_CMAKE_PATH "${WORK_DIR}" WORK_DIR_NORM)

function(write_case case_name header_text)
    set(header_path "${WORK_DIR_NORM}/api_${case_name}.h")
    set(tu_path "${WORK_DIR_NORM}/tu_${case_name}.cpp")
    set(compdb_path "${WORK_DIR_NORM}/compile_commands_${case_name}.json")

    file(WRITE "${header_path}" "${header_text}")
    file(WRITE "${tu_path}" "#include \"api_${case_name}.h\"\n")
    file(WRITE "${compdb_path}" "[
  {
    \"directory\": \"${WORK_DIR_NORM}\",
    \"file\": \"${tu_path}\",
    \"arguments\": [\"clang++\", \"-std=c++20\", \"-I${WORK_DIR_NORM}\", \"-c\", \"${tu_path}\"]
  }
]
")

    set(${case_name}_COMPDB "${compdb_path}" PARENT_SCOPE)
endfunction()

set(BASELINE_HEADER "#pragma once\n\nnamespace fixture {\n\nstruct Device\n{\n    int id;\n};\n\nint Compute(int value);\n\n} // namespace fixture\n")
set(CHANGED_HEADER "#pragma once\n\nnamespace fixture {\n\nstruct Device\n{\n    int id;\n    int serial;\n};\n\nint Compute(int value);\n\n} // namespace fixture\n")

write_case("baseline" "${BASELINE_HEADER}")
write_case("changed" "${CHANGED_HEADER}")

set(BASELINE_REPORT "${WORK_DIR_NORM}/baseline-ast.json")
set(CHANGED_REPORT "${WORK_DIR_NORM}/changed-ast.json")
set(META_FILE "${WORK_DIR_NORM}/build.env")
file(WRITE "${META_FILE}" "compiler=clang\nstd=c++20\n")

execute_process(
    COMMAND "${APISIG_EXE}" extract --compdb "${baseline_COMPDB}" --source-root "${WORK_DIR_NORM}" --no-tooling-banner --ast-report-out "${BASELINE_REPORT}" --json
    RESULT_VARIABLE baseline_extract_exit_code
    OUTPUT_VARIABLE baseline_extract_output
    ERROR_VARIABLE baseline_extract_error)
if(NOT baseline_extract_exit_code EQUAL 0)
    message(FATAL_ERROR "baseline extract failed\nstdout:\n${baseline_extract_output}\nstderr:\n${baseline_extract_error}")
endif()

execute_process(
    COMMAND "${APISIG_EXE}" extract --compdb "${changed_COMPDB}" --source-root "${WORK_DIR_NORM}" --no-tooling-banner --ast-report-out "${CHANGED_REPORT}" --json
    RESULT_VARIABLE changed_extract_exit_code
    OUTPUT_VARIABLE changed_extract_output
    ERROR_VARIABLE changed_extract_error)
if(NOT changed_extract_exit_code EQUAL 0)
    message(FATAL_ERROR "changed extract failed\nstdout:\n${changed_extract_output}\nstderr:\n${changed_extract_error}")
endif()

execute_process(
    COMMAND "${APISIG_EXE}" compare --ast-report-json "${BASELINE_REPORT}" --baseline-ast-report-json "${BASELINE_REPORT}" --json
    RESULT_VARIABLE same_compare_exit_code
    OUTPUT_VARIABLE same_compare_output
    ERROR_VARIABLE same_compare_error)
if(NOT same_compare_exit_code EQUAL 0)
    message(FATAL_ERROR "same-report compare failed\nstdout:\n${same_compare_output}\nstderr:\n${same_compare_error}")
endif()
string(JSON same_status GET "${same_compare_output}" status)
if(NOT same_status STREQUAL "unchanged")
    message(FATAL_ERROR "Expected unchanged status for identical AST reports, got ${same_status}\noutput:\n${same_compare_output}")
endif()

execute_process(
    COMMAND "${APISIG_EXE}" compare --ast-report-json "${CHANGED_REPORT}" --baseline-ast-report-json "${BASELINE_REPORT}" --json
    RESULT_VARIABLE changed_compare_exit_code
    OUTPUT_VARIABLE changed_compare_output
    ERROR_VARIABLE changed_compare_error)
if(NOT changed_compare_exit_code EQUAL 10)
    message(FATAL_ERROR "Expected api_changed exit code 10 for changed AST report, got ${changed_compare_exit_code}\nstdout:\n${changed_compare_output}\nstderr:\n${changed_compare_error}")
endif()
string(JSON changed_status GET "${changed_compare_output}" status)
if(NOT changed_status STREQUAL "api_changed")
    message(FATAL_ERROR "Expected api_changed status for changed AST report, got ${changed_status}\noutput:\n${changed_compare_output}")
endif()

execute_process(
    COMMAND "${APISIG_EXE}" compare --ast-report-json "${BASELINE_REPORT}" --baseline-ast-report-json "${BASELINE_REPORT}" --metadata "${META_FILE}" --json
    RESULT_VARIABLE metadata_compare_exit_code
    OUTPUT_VARIABLE metadata_compare_output
    ERROR_VARIABLE metadata_compare_error)
if(NOT metadata_compare_exit_code EQUAL 1)
    message(FATAL_ERROR "Expected metadata with AST baseline compare to fail with exit code 1, got ${metadata_compare_exit_code}\nstdout:\n${metadata_compare_output}\nstderr:\n${metadata_compare_error}")
endif()

message(STATUS "semantic model compare checks passed")
