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

set(TU_EXPLICIT "${FIXTURE_DIR_NORM}/tu_explicit.cpp")
set(TU_TRANSITIVE "${FIXTURE_DIR_NORM}/tu_transitive.cpp")
set(TU_CONDITIONAL "${FIXTURE_DIR_NORM}/tu_conditional.cpp")

set(COMPDB_OVERLAP "${WORK_DIR_NORM}/compile_commands_overlap.json")
set(COMPDB_OVERLAP_SHUFFLED "${WORK_DIR_NORM}/compile_commands_overlap_shuffled.json")
set(COMPDB_MINIMAL "${WORK_DIR_NORM}/compile_commands_minimal.json")
set(COMPDB_COND_INT "${WORK_DIR_NORM}/compile_commands_conditional_int.json")
set(COMPDB_COND_DOUBLE "${WORK_DIR_NORM}/compile_commands_conditional_double.json")

file(WRITE "${COMPDB_OVERLAP}" "[
  {
    \"directory\": \"${FIXTURE_DIR_NORM}\",
    \"file\": \"${TU_EXPLICIT}\",
    \"arguments\": [\"clang++\", \"-std=c++20\", \"-I${FIXTURE_DIR_NORM}\", \"-c\", \"${TU_EXPLICIT}\"]
  },
  {
    \"directory\": \"${FIXTURE_DIR_NORM}\",
    \"file\": \"${TU_TRANSITIVE}\",
    \"arguments\": [\"clang++\", \"-std=c++20\", \"-I${FIXTURE_DIR_NORM}\", \"-c\", \"${TU_TRANSITIVE}\"]
  },
  {
    \"directory\": \"${FIXTURE_DIR_NORM}\",
    \"file\": \"${TU_TRANSITIVE}\",
    \"arguments\": [\"clang++\", \"-std=c++20\", \"-I${FIXTURE_DIR_NORM}\", \"-c\", \"${TU_TRANSITIVE}\"]
  }
]
")

file(WRITE "${COMPDB_OVERLAP_SHUFFLED}" "[
  {
    \"directory\": \"${FIXTURE_DIR_NORM}\",
    \"file\": \"${TU_TRANSITIVE}\",
    \"arguments\": [\"clang++\", \"-std=c++20\", \"-I${FIXTURE_DIR_NORM}\", \"-c\", \"${TU_TRANSITIVE}\"]
  },
  {
    \"directory\": \"${FIXTURE_DIR_NORM}\",
    \"file\": \"${TU_EXPLICIT}\",
    \"arguments\": [\"clang++\", \"-std=c++20\", \"-I${FIXTURE_DIR_NORM}\", \"-c\", \"${TU_EXPLICIT}\"]
  },
  {
    \"directory\": \"${FIXTURE_DIR_NORM}\",
    \"file\": \"${TU_TRANSITIVE}\",
    \"arguments\": [\"clang++\", \"-std=c++20\", \"-I${FIXTURE_DIR_NORM}\", \"-c\", \"${TU_TRANSITIVE}\"]
  }
]
")

file(WRITE "${COMPDB_MINIMAL}" "[
  {
    \"directory\": \"${FIXTURE_DIR_NORM}\",
    \"file\": \"${TU_TRANSITIVE}\",
    \"arguments\": [\"clang++\", \"-std=c++20\", \"-I${FIXTURE_DIR_NORM}\", \"-c\", \"${TU_TRANSITIVE}\"]
  }
]
")

file(WRITE "${COMPDB_COND_INT}" "[
  {
    \"directory\": \"${FIXTURE_DIR_NORM}\",
    \"file\": \"${TU_CONDITIONAL}\",
    \"arguments\": [\"clang++\", \"-std=c++20\", \"-I${FIXTURE_DIR_NORM}\", \"-c\", \"${TU_CONDITIONAL}\"]
  }
]
")

file(WRITE "${COMPDB_COND_DOUBLE}" "[
  {
    \"directory\": \"${FIXTURE_DIR_NORM}\",
    \"file\": \"${TU_CONDITIONAL}\",
    \"arguments\": [\"clang++\", \"-std=c++20\", \"-DAPISIG_ALT_API\", \"-I${FIXTURE_DIR_NORM}\", \"-c\", \"${TU_CONDITIONAL}\"]
  }
]
")

function(run_compute compdb_path result_var)
    execute_process(
        COMMAND "${APISIG_EXE}" compute --compdb "${compdb_path}" --source-root "${FIXTURE_DIR_NORM}" --no-tooling-banner --json
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

    set(${result_var} "${parsed_hash}" PARENT_SCOPE)
endfunction()

run_compute("${COMPDB_OVERLAP}" HASH_OVERLAP)
run_compute("${COMPDB_OVERLAP_SHUFFLED}" HASH_OVERLAP_SHUFFLED)
run_compute("${COMPDB_MINIMAL}" HASH_MINIMAL)

if(NOT HASH_OVERLAP STREQUAL HASH_OVERLAP_SHUFFLED)
  message(FATAL_ERROR "Expected same api_hash for overlap compdb with shuffled entry order, but got ${HASH_OVERLAP} vs ${HASH_OVERLAP_SHUFFLED}")
endif()

if(NOT HASH_OVERLAP STREQUAL HASH_MINIMAL)
    message(FATAL_ERROR "Expected same api_hash for overlap/minimal compdb coverage, but got ${HASH_OVERLAP} vs ${HASH_MINIMAL}")
endif()

run_compute("${COMPDB_COND_INT}" HASH_COND_INT)
run_compute("${COMPDB_COND_DOUBLE}" HASH_COND_DOUBLE)

if(HASH_COND_INT STREQUAL HASH_COND_DOUBLE)
    message(FATAL_ERROR "Expected api_hash to change when APISIG_ALT_API changes public signature, but both are ${HASH_COND_INT}")
endif()

message(STATUS "compdb edge-case checks passed")
