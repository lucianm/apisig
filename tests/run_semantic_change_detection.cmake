cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED APISIG_EXE)
    message(FATAL_ERROR "APISIG_EXE is required")
endif()
if(NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "WORK_DIR is required")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
file(TO_CMAKE_PATH "${WORK_DIR}" WORK_DIR_NORM)

function(write_file path text)
    file(WRITE "${path}" "${text}")
endfunction()

function(write_case case_name header_text)
    set(header_path "${WORK_DIR_NORM}/api_${case_name}.h")
    set(tu_path "${WORK_DIR_NORM}/tu_${case_name}.cpp")
    set(compdb_path "${WORK_DIR_NORM}/compile_commands_${case_name}.json")

    write_file("${header_path}" "${header_text}")
    write_file("${tu_path}" "#include \"api_${case_name}.h\"\n")

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

function(run_compute compdb_path api_hash_var)
    execute_process(
        COMMAND "${APISIG_EXE}" compute --compdb "${compdb_path}" --source-root "${WORK_DIR_NORM}" --no-tooling-banner --json
        RESULT_VARIABLE exit_code
        OUTPUT_VARIABLE command_output
        ERROR_VARIABLE command_error)

    if(NOT exit_code EQUAL 0)
        message(FATAL_ERROR "apisig compute failed for ${compdb_path}\nstdout:\n${command_output}\nstderr:\n${command_error}")
    endif()

    string(JSON parsed_hash GET "${command_output}" api_hash)
    if(parsed_hash STREQUAL "")
        message(FATAL_ERROR "Could not parse api_hash from output for ${compdb_path}: ${command_output}")
    endif()

    set(${api_hash_var} "${parsed_hash}" PARENT_SCOPE)
endfunction()

function(assert_api_changed case_name compdb_path baseline_file baseline_hash)
    run_compute("${compdb_path}" case_hash)
    if(case_hash STREQUAL baseline_hash)
        message(FATAL_ERROR "Expected api_hash change for ${case_name}, but hash remained ${case_hash}")
    endif()

    execute_process(
        COMMAND "${APISIG_EXE}" compare --compdb "${compdb_path}" --source-root "${WORK_DIR_NORM}" --baseline "${baseline_file}" --no-tooling-banner --json
        RESULT_VARIABLE compare_exit_code
        OUTPUT_VARIABLE compare_output
        ERROR_VARIABLE compare_error)

    if(NOT compare_exit_code EQUAL 10)
        message(FATAL_ERROR "Expected compare exit code 10 (api_changed) for ${case_name}, got ${compare_exit_code}\nstdout:\n${compare_output}\nstderr:\n${compare_error}")
    endif()

    string(JSON compare_status GET "${compare_output}" status)
    if(NOT compare_status STREQUAL "api_changed")
        message(FATAL_ERROR "Expected compare status api_changed for ${case_name}, got ${compare_status}\noutput:\n${compare_output}")
    endif()
endfunction()

set(BASELINE_HEADER "#pragma once\n\nnamespace fixture {\n\nenum class Mode\n{\n    Off = 0,\n    On = 1\n};\n\nstruct Device\n{\n    int id;\n};\n\nint Compute(int value);\n\n} // namespace fixture\n")

set(CASE_ADD_MEMBER_HEADER "#pragma once\n\nnamespace fixture {\n\nenum class Mode\n{\n    Off = 0,\n    On = 1\n};\n\nstruct Device\n{\n    int id;\n    int serial;\n};\n\nint Compute(int value);\n\n} // namespace fixture\n")

set(CASE_CHANGE_MEMBER_TYPE_HEADER "#pragma once\n\nnamespace fixture {\n\nenum class Mode\n{\n    Off = 0,\n    On = 1\n};\n\nstruct Device\n{\n    long long id;\n};\n\nint Compute(int value);\n\n} // namespace fixture\n")

set(CASE_ADD_ENUM_VALUE_HEADER "#pragma once\n\nnamespace fixture {\n\nenum class Mode\n{\n    Off = 0,\n    On = 1,\n    Standby = 2\n};\n\nstruct Device\n{\n    int id;\n};\n\nint Compute(int value);\n\n} // namespace fixture\n")

set(CASE_CHANGE_ENUM_VALUE_HEADER "#pragma once\n\nnamespace fixture {\n\nenum class Mode\n{\n    Off = 0,\n    On = 5\n};\n\nstruct Device\n{\n    int id;\n};\n\nint Compute(int value);\n\n} // namespace fixture\n")

set(CASE_ADD_FUNCTION_HEADER "#pragma once\n\nnamespace fixture {\n\nenum class Mode\n{\n    Off = 0,\n    On = 1\n};\n\nstruct Device\n{\n    int id;\n};\n\nint Compute(int value);\nint ComputeEx(int value, Mode mode);\n\n} // namespace fixture\n")

set(CASE_CHANGE_FUNCTION_SIG_HEADER "#pragma once\n\nnamespace fixture {\n\nenum class Mode\n{\n    Off = 0,\n    On = 1\n};\n\nstruct Device\n{\n    int id;\n};\n\nint Compute(double value);\n\n} // namespace fixture\n")

write_case("baseline" "${BASELINE_HEADER}")
write_case("add_member" "${CASE_ADD_MEMBER_HEADER}")
write_case("change_member_type" "${CASE_CHANGE_MEMBER_TYPE_HEADER}")
write_case("add_enum_value" "${CASE_ADD_ENUM_VALUE_HEADER}")
write_case("change_enum_value" "${CASE_CHANGE_ENUM_VALUE_HEADER}")
write_case("add_function" "${CASE_ADD_FUNCTION_HEADER}")
write_case("change_function_sig" "${CASE_CHANGE_FUNCTION_SIG_HEADER}")

set(BASELINE_SNAPSHOT "${WORK_DIR_NORM}/baseline.json")
execute_process(
    COMMAND "${APISIG_EXE}" snapshot --compdb "${baseline_COMPDB}" --source-root "${WORK_DIR_NORM}" --out "${BASELINE_SNAPSHOT}" --no-tooling-banner
    RESULT_VARIABLE snapshot_exit_code
    OUTPUT_VARIABLE snapshot_output
    ERROR_VARIABLE snapshot_error)

if(NOT snapshot_exit_code EQUAL 0)
    message(FATAL_ERROR "apisig snapshot failed for baseline case\nstdout:\n${snapshot_output}\nstderr:\n${snapshot_error}")
endif()

run_compute("${baseline_COMPDB}" BASELINE_HASH)

assert_api_changed("add member in type" "${add_member_COMPDB}" "${BASELINE_SNAPSHOT}" "${BASELINE_HASH}")
assert_api_changed("change member type" "${change_member_type_COMPDB}" "${BASELINE_SNAPSHOT}" "${BASELINE_HASH}")
assert_api_changed("add enum value" "${add_enum_value_COMPDB}" "${BASELINE_SNAPSHOT}" "${BASELINE_HASH}")
assert_api_changed("change enum value" "${change_enum_value_COMPDB}" "${BASELINE_SNAPSHOT}" "${BASELINE_HASH}")
assert_api_changed("add function" "${add_function_COMPDB}" "${BASELINE_SNAPSHOT}" "${BASELINE_HASH}")
assert_api_changed("change function signature" "${change_function_sig_COMPDB}" "${BASELINE_SNAPSHOT}" "${BASELINE_HASH}")

message(STATUS "semantic change detection cases passed")
