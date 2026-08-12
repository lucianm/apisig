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

set(BASELINE_HEADER "#ifndef API_BASELINE_H\n#define API_BASELINE_H\n\n#define API_VERSION 1\n#define API_BUILD_ID(major, minor) ((major) * 100 + \\\n                                     (minor))\n#define API_STR(x) #x\n\nnamespace fixture {\n\nint Compute(int value);\n\n} // namespace fixture\n\n#endif // API_BASELINE_H\n")
set(CHANGED_HEADER "#ifndef API_CHANGED_H\n#define API_CHANGED_H\n\n#define API_VERSION 2\n#define API_BUILD_ID(major, minor) ((major) * 1000 + \\\n                                     (minor))\n#define API_STR(x) #x\n\nnamespace fixture {\n\nint Compute(int value);\n\n} // namespace fixture\n\n#endif // API_CHANGED_H\n")

write_case("baseline" "${BASELINE_HEADER}")
write_case("changed" "${CHANGED_HEADER}")

set(BASELINE_REPORT "${WORK_DIR_NORM}/baseline-ast.json")
set(CHANGED_REPORT "${WORK_DIR_NORM}/changed-ast.json")

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

file(READ "${BASELINE_REPORT}" baseline_report_text)
string(FIND "${baseline_report_text}" "\"symbol\": \"macro:API_VERSION\"" has_version_macro)
if(has_version_macro EQUAL -1)
    message(FATAL_ERROR "Expected API_VERSION macro in AST report\n${baseline_report_text}")
endif()
string(FIND "${baseline_report_text}" "\"symbol\": \"macro:API_BUILD_ID\"" has_function_macro)
if(has_function_macro EQUAL -1)
    message(FATAL_ERROR "Expected API_BUILD_ID macro in AST report\n${baseline_report_text}")
endif()
string(FIND "${baseline_report_text}" "\"symbol\": \"macro:API_BASELINE_H\"" has_include_guard_macro)
if(NOT has_include_guard_macro EQUAL -1)
    message(FATAL_ERROR "Include guard macro must be excluded from AST report\n${baseline_report_text}")
endif()

execute_process(
    COMMAND "${APISIG_EXE}" compute --compdb "${baseline_COMPDB}" --source-root "${WORK_DIR_NORM}" --no-tooling-banner --json
    RESULT_VARIABLE compdb_exit_code
    OUTPUT_VARIABLE compdb_output
    ERROR_VARIABLE compdb_error)
if(NOT compdb_exit_code EQUAL 0)
    message(FATAL_ERROR "compute(compdb) failed\nstdout:\n${compdb_output}\nstderr:\n${compdb_error}")
endif()
string(JSON compdb_hash GET "${compdb_output}" api_hash)

execute_process(
    COMMAND "${APISIG_EXE}" compute --ast-report-json "${BASELINE_REPORT}" --json
    RESULT_VARIABLE report_exit_code
    OUTPUT_VARIABLE report_output
    ERROR_VARIABLE report_error)
if(NOT report_exit_code EQUAL 0)
    message(FATAL_ERROR "compute(ast-report-json) failed\nstdout:\n${report_output}\nstderr:\n${report_error}")
endif()
string(JSON report_hash GET "${report_output}" api_hash)

if(NOT compdb_hash STREQUAL report_hash)
    message(FATAL_ERROR "Expected same api_hash from compdb and ast-report-json, got ${compdb_hash} vs ${report_hash}")
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

message(STATUS "macro semantic model checks passed")
