cmake_minimum_required(VERSION 3.22)

project(apisig VERSION 0.1.0 LANGUAGES CXX)

set(APISIG_VERSION_TWEAK "${PROJECT_VERSION_TWEAK}")
if(APISIG_VERSION_TWEAK STREQUAL "")
    set(APISIG_VERSION_TWEAK "0")
endif()

set(APISIG_FILE_VERSION "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},${APISIG_VERSION_TWEAK}")
set(APISIG_FILE_VERSION_STR "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.${APISIG_VERSION_TWEAK}")

option(APISIG_SELF_CONTAINED "Build apisig without runtime dependency on clang libraries" ON)
option(APISIG_WITH_LIBTOOLING "Enable clang LibTooling integration" OFF)
option(APISIG_DOWNLOAD_LLVM_SDK "Download a prebuilt official LLVM SDK for LibTooling builds" OFF)

set(APISIG_LLVM_ROOT "" CACHE PATH "Root directory of an installed or extracted LLVM/Clang SDK")
set(APISIG_LLVM_SDK_VERSION "22.1.8" CACHE STRING "Official LLVM SDK version used for auto-download")
set(APISIG_LLVM_SDK_SHA256 "d96c2cc1736f4eb7fa43cb9bbdf56d93551a9ae0a9aadb9c99c3c3b2b712a234" CACHE STRING "Expected SHA256 for the official LLVM SDK archive")
set(APISIG_LLVM_SDK_URL "" CACHE STRING "Override URL for the LLVM SDK archive")
set(APISIG_LLVM_SDK_CACHE_DIR "${CMAKE_SOURCE_DIR}/out/llvm-sdk" CACHE PATH "Directory used to cache downloaded LLVM SDK archives")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

function(apisig_configure_llvm_sdk)
    if(APISIG_LLVM_ROOT)
        return()
    endif()

    if(NOT APISIG_DOWNLOAD_LLVM_SDK)
        return()
    endif()

    if(NOT WIN32)
        message(FATAL_ERROR "apisig: APISIG_DOWNLOAD_LLVM_SDK currently supports only the Windows presets. Set APISIG_LLVM_ROOT manually on this platform.")
    endif()

    set(archive_url "${APISIG_LLVM_SDK_URL}")
    if(archive_url STREQUAL "")
        set(archive_url "https://github.com/llvm/llvm-project/releases/download/llvmorg-${APISIG_LLVM_SDK_VERSION}/clang+llvm-${APISIG_LLVM_SDK_VERSION}-x86_64-pc-windows-msvc.tar.xz")
    endif()

    get_filename_component(archive_name "${archive_url}" NAME)
    set(cache_root "${APISIG_LLVM_SDK_CACHE_DIR}/${APISIG_LLVM_SDK_VERSION}")
    set(archive_path "${cache_root}/${archive_name}")
    set(extract_root "${cache_root}/extract")
    set(marker_path "${cache_root}/sdk-root.txt")

    if(EXISTS "${marker_path}")
        file(READ "${marker_path}" cached_root)
        string(STRIP "${cached_root}" cached_root)
        if(EXISTS "${cached_root}/lib/cmake/llvm/LLVMConfig.cmake")
            set(APISIG_LLVM_ROOT "${cached_root}" CACHE PATH "Root directory of an installed or extracted LLVM/Clang SDK" FORCE)
            return()
        endif()
    endif()

    file(MAKE_DIRECTORY "${cache_root}")

    if(NOT EXISTS "${archive_path}")
        message(STATUS "apisig: downloading official LLVM SDK ${APISIG_LLVM_SDK_VERSION}")
        if(APISIG_LLVM_SDK_SHA256)
            file(DOWNLOAD "${archive_url}" "${archive_path}"
                EXPECTED_HASH "SHA256=${APISIG_LLVM_SDK_SHA256}"
                SHOW_PROGRESS
                STATUS download_status)
        else()
            file(DOWNLOAD "${archive_url}" "${archive_path}"
                SHOW_PROGRESS
                STATUS download_status)
        endif()

        list(GET download_status 0 download_code)
        list(GET download_status 1 download_message)
        if(NOT download_code EQUAL 0)
            message(FATAL_ERROR "apisig: failed to download LLVM SDK: ${download_message}")
        endif()
    endif()

    file(REMOVE_RECURSE "${extract_root}")
    file(MAKE_DIRECTORY "${extract_root}")
    message(STATUS "apisig: extracting LLVM SDK archive ${archive_name}")
    file(ARCHIVE_EXTRACT INPUT "${archive_path}" DESTINATION "${extract_root}")

    set(sdk_root "")
    if(EXISTS "${extract_root}/lib/cmake/llvm/LLVMConfig.cmake")
        set(sdk_root "${extract_root}")
    else()
        file(GLOB extracted_entries LIST_DIRECTORIES true "${extract_root}/*")
        foreach(entry IN LISTS extracted_entries)
            if(EXISTS "${entry}/lib/cmake/llvm/LLVMConfig.cmake")
                set(sdk_root "${entry}")
                break()
            endif()
        endforeach()
    endif()

    if(sdk_root STREQUAL "")
        message(FATAL_ERROR "apisig: extracted LLVM SDK does not contain lib/cmake/llvm/LLVMConfig.cmake")
    endif()

    file(WRITE "${marker_path}" "${sdk_root}\n")
    set(APISIG_LLVM_ROOT "${sdk_root}" CACHE PATH "Root directory of an installed or extracted LLVM/Clang SDK" FORCE)
endfunction()

function(apisig_fix_llvm_dia_dependency)
    if(NOT TARGET LLVMDebugInfoPDB)
        return()
    endif()

    get_target_property(pdb_links LLVMDebugInfoPDB INTERFACE_LINK_LIBRARIES)
    if(NOT pdb_links)
        return()
    endif()

    set(expected_dia_path "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/DIA SDK/lib/amd64/diaguids.lib")
    if(NOT pdb_links MATCHES "Enterprise/DIA SDK/lib/amd64/diaguids\\.lib" OR EXISTS "${expected_dia_path}")
        return()
    endif()

    file(GLOB dia_candidates "C:/Program Files/Microsoft Visual Studio/2022/*/DIA SDK/lib/amd64/diaguids.lib")
    list(LENGTH dia_candidates dia_candidate_count)
    if(dia_candidate_count EQUAL 0)
        return()
    endif()

    list(GET dia_candidates 0 resolved_dia_path)
    string(REPLACE "${expected_dia_path}" "${resolved_dia_path}" pdb_links "${pdb_links}")
    set_target_properties(LLVMDebugInfoPDB PROPERTIES INTERFACE_LINK_LIBRARIES "${pdb_links}")
    message(STATUS "apisig: remapped LLVM DIA dependency to ${resolved_dia_path}")
endfunction()

add_library(apisig_core
    src/Hash.cpp
    src/Input.cpp
    src/SignatureEngine.cpp
    src/ClangExtractor.cpp
)

target_include_directories(apisig_core PUBLIC include)

function(apisig_enable_project_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE
            /W4
            /external:W0
            /external:anglebrackets
            /external:templates-)
    else()
        target_compile_options(${target_name} PRIVATE
            -Wall
            -Wextra
            -Wpedantic)
    endif()
endfunction()

apisig_enable_project_warnings(apisig_core)

if(MSVC)
    set_target_properties(apisig_core PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(APISIG_LIBTOOLING_ENABLED OFF)

if(APISIG_SELF_CONTAINED)
    if(APISIG_WITH_LIBTOOLING)
        message(STATUS "apisig: APISIG_SELF_CONTAINED=ON overrides APISIG_WITH_LIBTOOLING=ON")
    endif()
    set(APISIG_WITH_LIBTOOLING OFF)
    message(STATUS "apisig: self-contained mode enabled (no clang runtime dependency)")
endif()

if(APISIG_WITH_LIBTOOLING)
    apisig_configure_llvm_sdk()

    if(APISIG_LLVM_ROOT)
        file(TO_CMAKE_PATH "${APISIG_LLVM_ROOT}" APISIG_LLVM_ROOT_NORMALIZED)
        set(LLVM_DIR "${APISIG_LLVM_ROOT_NORMALIZED}/lib/cmake/llvm" CACHE PATH "Path to LLVMConfig.cmake" FORCE)
        set(Clang_DIR "${APISIG_LLVM_ROOT_NORMALIZED}/lib/cmake/clang" CACHE PATH "Path to ClangConfig.cmake" FORCE)
        list(PREPEND CMAKE_PREFIX_PATH "${APISIG_LLVM_ROOT_NORMALIZED}")
        message(STATUS "apisig: using LLVM SDK root ${APISIG_LLVM_ROOT_NORMALIZED}")
    endif()

    find_package(LLVM CONFIG QUIET)
    apisig_fix_llvm_dia_dependency()

    if(WIN32)
        find_package(Clang CONFIG QUIET)
        if(Clang_FOUND AND TARGET clangTooling)
            set(APISIG_LIBTOOLING_ENABLED ON)
            if(DEFINED LLVM_INCLUDE_DIRS)
                target_include_directories(apisig_core SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
            endif()
            if(DEFINED CLANG_INCLUDE_DIRS)
                target_include_directories(apisig_core SYSTEM PRIVATE ${CLANG_INCLUDE_DIRS})
            endif()
            if(MSVC)
                foreach(external_include_dir IN LISTS LLVM_INCLUDE_DIRS CLANG_INCLUDE_DIRS)
                    target_compile_options(apisig_core PRIVATE "/external:I${external_include_dir}")
                endforeach()
                set_source_files_properties(src/ClangExtractor.cpp PROPERTIES COMPILE_OPTIONS "/wd4702;/wd4100;/wd4456")
            endif()
            target_link_libraries(apisig_core PUBLIC clangTooling clangBasic clangIndex)
            message(STATUS "apisig: LibTooling enabled")
        else()
            message(STATUS "apisig: LibTooling requested but not found, using file-driven mode")
        endif()
    else()
        find_library(APISIG_CLANG_CPP_LIBRARY NAMES clang-cpp PATHS ${LLVM_LIBRARY_DIRS})
        if(APISIG_CLANG_CPP_LIBRARY)
            set(APISIG_LIBTOOLING_ENABLED ON)
            if(DEFINED LLVM_INCLUDE_DIRS)
                target_include_directories(apisig_core SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
            endif()
            target_link_libraries(apisig_core PUBLIC ${APISIG_CLANG_CPP_LIBRARY})
            message(STATUS "apisig: LibTooling enabled via ${APISIG_CLANG_CPP_LIBRARY}")
        else()
            message(STATUS "apisig: LibTooling requested but clang-cpp was not found, using file-driven mode")
        endif()
    endif()
endif()

target_compile_definitions(apisig_core
    PUBLIC
        APISIG_VERSION="${PROJECT_VERSION}"
        APISIG_HAVE_LIBTOOLING=$<BOOL:${APISIG_LIBTOOLING_ENABLED}>
)

set(APISIG_WIN_RESOURCE "")
if(WIN32)
    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/src/apisig_version.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/apisig_version.rc"
        @ONLY)
    set(APISIG_WIN_RESOURCE "${CMAKE_CURRENT_BINARY_DIR}/apisig_version.rc")
endif()

add_executable(apisig
    src/Cli.cpp
    src/main.cpp
    ${APISIG_WIN_RESOURCE}
)

apisig_enable_project_warnings(apisig)

target_link_libraries(apisig PRIVATE apisig_core)

if(MSVC)
    set_target_properties(apisig PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(CTest)
if(BUILD_TESTING AND APISIG_LIBTOOLING_ENABLED)
    add_test(
        NAME apisig.compdb_hash_edge_cases
        COMMAND ${CMAKE_COMMAND}
            -DAPISIG_EXE=$<TARGET_FILE:apisig>
            -DFIXTURE_DIR=${CMAKE_SOURCE_DIR}/tests/fixtures
            -DWORK_DIR=${CMAKE_BINARY_DIR}/tests/compdb-hash-edge-cases
            -P ${CMAKE_SOURCE_DIR}/tests/run_compdb_hash_edge_cases.cmake)

    add_test(
        NAME apisig.semantic_change_detection
        COMMAND ${CMAKE_COMMAND}
            -DAPISIG_EXE=$<TARGET_FILE:apisig>
            -DWORK_DIR=${CMAKE_BINARY_DIR}/tests/semantic-change-detection
            -P ${CMAKE_SOURCE_DIR}/tests/run_semantic_change_detection.cmake)

    add_test(
        NAME apisig.semantic_model_deserialization
        COMMAND ${CMAKE_COMMAND}
            -DAPISIG_EXE=$<TARGET_FILE:apisig>
            -DFIXTURE_DIR=${CMAKE_SOURCE_DIR}/tests/fixtures
            -DWORK_DIR=${CMAKE_BINARY_DIR}/tests/semantic-model-deserialization
            -P ${CMAKE_SOURCE_DIR}/tests/run_semantic_model_deserialization.cmake)

    add_test(
        NAME apisig.semantic_model_compare
        COMMAND ${CMAKE_COMMAND}
            -DAPISIG_EXE=$<TARGET_FILE:apisig>
            -DWORK_DIR=${CMAKE_BINARY_DIR}/tests/semantic-model-compare
            -P ${CMAKE_SOURCE_DIR}/tests/run_semantic_model_compare.cmake)
endif()
