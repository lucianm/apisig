cmake_minimum_required(VERSION 3.22)

project(apisig VERSION 0.1.0 LANGUAGES CXX)

option(APISIG_SELF_CONTAINED "Build apisig without runtime dependency on clang libraries" ON)
option(APISIG_WITH_LIBTOOLING "Enable clang LibTooling integration" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(apisig_core
    src/Hash.cpp
    src/Input.cpp
    src/SignatureEngine.cpp
    src/ClangExtractor.cpp
)

target_include_directories(apisig_core PUBLIC include)

set(APISIG_LIBTOOLING_ENABLED OFF)

if(APISIG_SELF_CONTAINED)
    if(APISIG_WITH_LIBTOOLING)
        message(STATUS "apisig: APISIG_SELF_CONTAINED=ON overrides APISIG_WITH_LIBTOOLING=ON")
    endif()
    set(APISIG_WITH_LIBTOOLING OFF)
    message(STATUS "apisig: self-contained mode enabled (no clang runtime dependency)")
endif()

if(APISIG_WITH_LIBTOOLING)
    find_package(Clang CONFIG QUIET)
    if(Clang_FOUND AND TARGET clangTooling)
        set(APISIG_LIBTOOLING_ENABLED ON)
        target_link_libraries(apisig_core PUBLIC clangTooling clangBasic)
        message(STATUS "apisig: LibTooling enabled")
    else()
        message(STATUS "apisig: LibTooling requested but not found, using file-driven mode")
    endif()
endif()

target_compile_definitions(apisig_core
    PUBLIC
        APISIG_VERSION="${PROJECT_VERSION}"
        APISIG_HAVE_LIBTOOLING=$<BOOL:${APISIG_LIBTOOLING_ENABLED}>
)

add_executable(apisig
    src/Cli.cpp
    src/main.cpp
)

target_link_libraries(apisig PRIVATE apisig_core)
