cmake_minimum_required(VERSION 3.24)

# Version info

project(ShaderTranspilerCore VERSION 0.1.0)
set(STC_VERSION_SUFFIX "")
set(STC_SOVERSION "0") # for lib ABI-compatibility

set(STC_VERSION "${PROJECT_VERSION}${STC_VERSION_SUFFIX}")

find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE STC_GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    set(STC_GIT_HASH "unknown")
endif()

set(STC_FULL_VERSION "${STC_VERSION}+${STC_GIT_HASH}")

message(STATUS "Build version is ${STC_FULL_VERSION}")

# Config

option(FETCH_CATCH2 "Force fetching Catch2 from the github repo, even if it's installed locally" OFF)
option(NO_CCACHE "Disable compiler caching through ccache, even if it's installed locally" OFF)
option(NO_TIDY "Disable static analysis through clang-tidy, even if it's installed locally" OFF)
option(NO_SAN "Disable address and undefined sanitizers (ASan and UBSan). Might be required with some versions of Julia")

if ("${CMAKE_BUILD_TYPE}" STREQUAL "")
    message(STATUS "No build type specified, defaulting to Debug")
    set(CMAKE_BUILD_TYPE "Debug")
endif()

message(STATUS "Build type is ${CMAKE_BUILD_TYPE}")

# Julia lookup

# make sure julia executable is available
find_program(JULIA_EXEC julia)
if (NOT JULIA_EXEC)
    message(FATAL_ERROR "Couldn't locate Julia executable (needed for locating libjulia). Please ensure Julia is installed and added to PATH.")
endif()

# find and include Julia lib and headers
execute_process(
    COMMAND ${JULIA_EXEC} -e "print(normpath(joinpath(Sys.BINDIR, \"..\", \"include\", \"julia\")))"
    OUTPUT_VARIABLE JULIA_INCLUDE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND ${JULIA_EXEC} -e "using Libdl; print(Libdl.dlpath(\"libjulia\"))"
    OUTPUT_VARIABLE JULIA_LIB
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND ${JULIA_EXEC} -e "print(VERSION)"
    OUTPUT_VARIABLE STC_JULIA_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

get_filename_component(JULIA_BINDIR ${JULIA_LIB} DIRECTORY)
set(JULIA_LIBDIR "${JULIA_BINDIR}/../lib")
get_filename_component(JULIA_LIBDIR ${JULIA_LIBDIR} ABSOLUTE)

# on windows with msvc, we can't link the dll itself (because of reasons), we need the dll.a (or .lib)
if (WIN32)
    set(JULIA_LIB "${JULIA_LIBDIR}/libjulia.dll.a")
    if (NOT EXISTS "${JULIA_LIB}")
        set(JULIA_LIB "${JULIA_LIBDIR}/julia.lib")
    endif()

    if (NOT EXISTS "${JULIA_LIB}")
        message(FATAL_ERROR "Couldn't locate Julia library (libjulia.dll.a or julia.lib)")
    endif()
endif()

if (NOT EXISTS "${JULIA_LIB}")
    message(FATAL_ERROR "Couldn't locate libjulia")
endif()

if (NOT EXISTS "${JULIA_INCLUDE_DIR}")
    message(FATAL_ERROR "Couldn't locate libjulia's include directory")
endif()

message(STATUS "Found Julia include dir: ${JULIA_INCLUDE_DIR}")
message(STATUS "Found Julia library: ${JULIA_LIB}")

# C++ building begins here

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

set(LIB_OUTPUT_DIR ${CMAKE_BINARY_DIR}/lib)
set(BIN_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin)
set(TEST_OUTPUT_DIR ${CMAKE_BINARY_DIR}/test)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/meta/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/meta/version.h"
)

file(GLOB_RECURSE ALL_SOURCES CONFIGURE_DEPENDS "include/*.h" "src/*.cpp" "cli/*.h" "cli/*.cpp" "sandbox/*.cpp" "sandbox/*.h" "test/*.cpp" "test/*.h")

set(LIB_SOURCES ${ALL_SOURCES})
list(FILTER LIB_SOURCES EXCLUDE REGEX "sandbox/.*")
list(FILTER LIB_SOURCES EXCLUDE REGEX "test/.*")
list(FILTER LIB_SOURCES EXCLUDE REGEX "cli/.*")

# ccache

if (NOT NO_CCACHE)
    find_program(CCACHE_EXEC ccache)
    if (CCACHE_EXEC)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_EXEC}")
        set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_EXEC}")

        message(STATUS "ccache found and enabled for compiler caching")
    else()
        message(STATUS "ccache not found, compiler caching won't be possible")
    endif()
else()
    message(STATUS "ccache disabled explicitly")
endif()

# clang-format

find_program(CLANG_FORMAT_EXEC NAMES clang-format)
if (CLANG_FORMAT_EXEC)
    # style checking
    add_custom_target(
        "check_format"
        COMMAND ${CLANG_FORMAT_EXEC}
                -style=file
                -n
                --Werror
                ${ALL_SOURCES}
        COMMENT "Checking C++ code style..."
    )

    # automatic style enforcement
    add_custom_target(
        "fix_format"
        COMMAND ${CLANG_FORMAT_EXEC}
                -style=file
                -i
                ${ALL_SOURCES}
        COMMENT "Fixing C++ code style..."
    )

    message(STATUS "clang-format found, check_format and fix_format targets enabled. Using ${CLANG_FORMAT_EXEC}")
else()
    message(STATUS "clang-format could not be located, code style checking won't be possible.")
endif()

# clang-tidy

if (NOT NO_TIDY)
    find_program(CLANG_TIDY_EXEC clang-tidy)

    if (CLANG_TIDY_EXEC)
        set(CLANG_TIDY_CMD "${CLANG_TIDY_EXEC}")

        list(APPEND CLANG_TIDY_CMD "--extra-arg=-Wno-unknown-warning-option")
        list(APPEND CLANG_TIDY_CMD "--header-filter=^${CMAKE_SOURCE_DIR}/*.h")

        if (MSVC)
            list(APPEND CLANG_TIDY_CMD "--extra-arg-before=--driver-mode=cl")
            list(APPEND CLANG_TIDY_CMD "--extra-arg=-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH")
            list(APPEND CLANG_TIDY_CMD "--extra-arg=-Wno-unused-command-line-argument")
            list(APPEND CLANG_TIDY_CMD "--extra-arg=/EHsc")
        endif()

        message(STATUS "clang-tidy found and enabled. Using ${CLANG_TIDY_EXEC}")
    else()
        message(STATUS "clang-tidy could not be located, static analysis won't be possible.")
    endif()
else()
    message(STATUS "clang-tidy disabled explicitly")
endif()

add_library(CommonConfig INTERFACE)

# TODO: use symbol hiding by default, once the lib API is in a semi-stable state
# it would also be nice to side step potential issues with this for sandboxes

if (MSVC)
    # apparently MSVC preprocessor is not standard-confirming by default
    # /Zc:preprocessor needed for X-macro system (__VA_ARGS__ expansion)
    target_compile_options(CommonConfig INTERFACE /W4 /Zc:preprocessor /GR-)
else()
    target_compile_options(CommonConfig INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wreorder
        -fno-rtti
        # $<$<NOT:$<CONFIG:Debug>>:-fvisibility=hidden>
    )

    if (MINGW)
        target_link_options(CommonConfig INTERFACE -static-libgcc -static-libstdc++ -static -lpthread -Wl,--allow-multiple-definition)
    endif()
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT NO_SAN)
    # enable asan (and force disable incompatible options on MSVC)
    if (MSVC)
        target_compile_options(CommonConfig INTERFACE /fsanitize=address)
        target_compile_options(CommonConfig INTERFACE /Zi)

        string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        string(REPLACE "/ZI" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")

        string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
        string(REPLACE "/ZI" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")

        target_link_options(CommonConfig INTERFACE "/INCREMENTAL:NO")
        message(STATUS "ASan enabled for MSVC (detected ${CMAKE_SYSTEM_NAME})")
    elseif (UNIX)
        target_compile_options(CommonConfig INTERFACE
            -fsanitize=address,undefined -fno-omit-frame-pointer -g
        )

        target_link_options(CommonConfig INTERFACE -fsanitize=address,undefined)
        message(STATUS "ASan and UBSan enabled for GCC/Clang/etc (detected ${CMAKE_SYSTEM_NAME})")
    else()
        message(STATUS "ASan not supported on the host system (${CMAKE_SYSTEM_NAME})")
    endif()
endif()

# libstc

file(GLOB_RECURSE LIB_HEADERS CONFIGURE_DEPENDS "include/*.h")

set(LIB_TARGET stc)

add_library(${LIB_TARGET} SHARED ${LIB_SOURCES})

target_include_directories(${LIB_TARGET} PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(${LIB_TARGET} PUBLIC  ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_include_directories(${LIB_TARGET} SYSTEM PRIVATE ${JULIA_INCLUDE_DIR})

target_link_libraries(${LIB_TARGET} PRIVATE CommonConfig)
target_link_libraries(${LIB_TARGET} PRIVATE ${JULIA_LIB})

target_compile_definitions(${LIB_TARGET} PRIVATE STC_BUILDING_DLL=1 JULIA_ENABLE_THREADING=1)

target_sources(${LIB_TARGET} PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES ${LIB_HEADERS}
)

set_target_properties(${LIB_TARGET} PROPERTIES
    VERSION                                 ${PROJECT_VERSION}
    SOVERSION                               ${STC_SOVERSION}

    LIBRARY_OUTPUT_DIRECTORY                ${LIB_OUTPUT_DIR}
    ARCHIVE_OUTPUT_DIRECTORY                ${LIB_OUTPUT_DIR}

    RUNTIME_OUTPUT_DIRECTORY_DEBUG          ${LIB_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE        ${LIB_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${LIB_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     ${LIB_OUTPUT_DIR}

    ARCHIVE_OUTPUT_DIRECTORY_DEBUG          ${LIB_OUTPUT_DIR}
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE        ${LIB_OUTPUT_DIR}
    ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO ${LIB_OUTPUT_DIR}
    ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL     ${LIB_OUTPUT_DIR}

    VERIFY_INTERFACE_HEADER_SETS            ON
)

if (DEFINED CLANG_TIDY_CMD AND NOT NO_TIDY)
    set_target_properties(${LIB_TARGET} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_CMD}")
endif()

# CLI

set(CLI_TARGET stc_cli)

add_executable(${CLI_TARGET} "cli/cli.cpp")

set_target_properties(${CLI_TARGET} PROPERTIES OUTPUT_NAME "stc")

set_target_properties(${CLI_TARGET} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG          ${BIN_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE        ${BIN_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${BIN_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     ${BIN_OUTPUT_DIR}
)

target_link_libraries(${CLI_TARGET} PRIVATE ${LIB_TARGET} CommonConfig ${JULIA_LIB})

target_include_directories(${CLI_TARGET} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(${CLI_TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_include_directories(${CLI_TARGET} SYSTEM PRIVATE ${JULIA_INCLUDE_DIR})

if (DEFINED CLANG_TIDY_CMD AND NOT NO_TIDY)
    set_target_properties(${CLI_TARGET} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_CMD}")
endif()

if (WIN32)
    add_custom_command(TARGET ${CLI_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:${LIB_TARGET}>
        $<TARGET_FILE_DIR:${CLI_TARGET}>
    )
endif()

# Catch2 / testing

if (BUILD_TESTING)
    if (NOT FETCH_CATCH2)
        find_package(Catch2 3 CONFIG QUIET)

        if (NOT TARGET Catch2::Catch2WithMain)
            message(STATUS "Catch2 not found on host, Catch2 will be fetched through git...")
        endif()
    else()
        message(STATUS "FETCH_CATCH flag enabled, Catch2 will be fetched through git...")
    endif()

    if (FETCH_CATCH2 OR NOT TARGET Catch2::Catch2WithMain)
        set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/_deps_${CMAKE_SYSTEM_NAME}")

        include(FetchContent)

        FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG        v3.12.0
        )

        FetchContent_MakeAvailable(Catch2)
    endif()

    if (NOT TARGET Catch2::Catch2WithMain)
        message(FATAL_ERROR "Catch2 could not be located or fetched, but testing has been explicitly requested.")
    endif()

    add_executable(unit_tests "test/test.cpp")

    set_target_properties(unit_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG          ${TEST_OUTPUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY_RELEASE        ${TEST_OUTPUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${TEST_OUTPUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     ${TEST_OUTPUT_DIR}
    )

    target_link_libraries(unit_tests PRIVATE ${LIB_TARGET} Catch2::Catch2WithMain CommonConfig)
    target_include_directories(unit_tests PRIVATE include)
    target_include_directories(unit_tests SYSTEM PRIVATE ${JULIA_INCLUDE_DIR})

    # copy lib for test runner
    if (WIN32)
        add_custom_command(TARGET unit_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${LIB_TARGET}>
            $<TARGET_FILE_DIR:unit_tests>
        )
    endif()

    # CTest
    include(CTest)
    enable_testing()
    add_test(NAME AllTests COMMAND $<TARGET_FILE:unit_tests>)

    message(STATUS "Catch2 found, unit_tests target enabled for testing")
else()
    message(STATUS "Skipping Catch2 build and unit_tests target")
endif()

# Doxygen (+ Graphviz)
find_package(Doxygen COMPONENTS dot)
if (Doxygen_FOUND)
    set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/docs")

    set(DOXYGEN_GENERATE_HTML YES)

    set(DOXYGEN_GENERATE_LATEX YES)
    set(DOXYGEN_USE_PDFLATEX YES)
    set(DOXYGEN_PDF_HYPERLINKS YES)

    set(DOXYGEN_RECURSIVE YES)

    # Graphviz
    set(DOXYGEN_HAVE_DOT YES)
    set(DOXYGEN_DOT_IMAGE_FORMAT svg)
    set(DOXYGEN_DOT_TRANSPARENT YES)
    set(DOXYGEN_CALL_GRAPH YES)
    set(DOXYGEN_CALLER_GRAPH YES)
    set(DOXYGEN_DIRECTORY_GRAPH YES)

    set(DOXYGEN_UML_LOOK YES)
    set(DOXYGEN_TEMPLATE_RELATIONS YES)
    set(DOXYGEN_INCLUDE_GRAPH YES)
    set(DOXYGEN_INTERACTIVE_SVG YES)

    set(DOXYGEN_EXTRACT_ALL YES)
    set(DOXYGEN_EXTRACT_PRIVATE YES)
    set(DOXYGEN_EXTRACT_STATIC YES)

    doxygen_add_docs(
        docs
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        COMMENT "Generating HTML and LaTeX docs with doxygen..."
    )

    message(STATUS "doxygen and graphviz found, docs target enabled")

    find_program(PDFLATEX_COMPILER pdflatex)

    if (PDFLATEX_COMPILER)
        if (WIN32)
            set(MAKE_CMD "make.bat")
        else()
            set(MAKE_CMD "make")
        endif()

        doxygen_add_docs(
            docs_pdf
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            COMMENT "Generating HTML and LaTeX docs with doxygen..."
        )

        add_custom_command(TARGET docs_pdf POST_BUILD
            COMMAND ${MAKE_CMD}
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/docs/latex"
            COMMENT "Compiling LaTeX docs to PDF..."
            VERBATIM
        )

        message(STATUS "pdflatex found, automatic LaTeX -> PDF docs generation enabled for docs_pdf target")
    else()
        message(STATUS "pdflatex could not be located, PDF generation from LaTeX docs won't be possible.")
    endif()
else()
    message(STATUS "doxygen or graphviz could not be located, docs generation won't be possible.")
endif()

# sandbox targets
# for trying out and playing around with the library
# cpp files prefixed with sb_ are treated as separate targets
# other cpp files can be used for includes and such

file(GLOB SANDBOX_SOURCES "sandbox/sb_*.cpp")

foreach (SBOX_FILE ${SANDBOX_SOURCES})
    get_filename_component(FNAME ${SBOX_FILE} NAME_WE)

    add_executable(${FNAME} ${SBOX_FILE})

    set_target_properties(${FNAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG          ${BIN_OUTPUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY_RELEASE        ${BIN_OUTPUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${BIN_OUTPUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     ${BIN_OUTPUT_DIR}
    )

    target_link_libraries(${FNAME} PRIVATE ${LIB_TARGET} CommonConfig)
    target_link_libraries(${FNAME} PRIVATE ${JULIA_LIB})

    target_include_directories(${FNAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_include_directories(${FNAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
    target_include_directories(${FNAME} SYSTEM PRIVATE ${JULIA_INCLUDE_DIR})

    if (DEFINED CLANG_TIDY_CMD AND NOT NO_TIDY)
        set_target_properties(${FNAME} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_CMD}")
    endif()

    # copy lib next to sandbox executable
    if (WIN32)
        add_custom_command(TARGET ${FNAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${LIB_TARGET}>
            $<TARGET_FILE_DIR:${FNAME}>
        )
    endif()

    # create wrappers for executable, with Julia first in PATH
    if (WIN32)
        file(TO_NATIVE_PATH "${JULIA_BINDIR}" JULIA_BINDIR_NATIVE)
        
        file(GENERATE OUTPUT "${BIN_OUTPUT_DIR}/run_${FNAME}.bat"
            CONTENT "@echo off\nset PATH=${JULIA_BINDIR_NATIVE};%PATH%\n\"$<TARGET_FILE:${FNAME}>\" %*"
            CONDITION $<CONFIG:${CMAKE_BUILD_TYPE}>
        )
    endif()
endforeach()
