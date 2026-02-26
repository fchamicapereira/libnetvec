###############################################################################
# Project version
###############################################################################

cmake_minimum_required(VERSION 3.16)

project(
    libnetvec
    VERSION 1.0
    LANGUAGES C CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

###############################################################################
# Configuring C/C++ standard versions
###############################################################################

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)
set(CMAKE_C_EXTENSIONS True)

# Check if the compiler is GCC
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # Check if the version is less than 13.0
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13.0)
        message(FATAL_ERROR 
            "You are using GCC version ${CMAKE_CXX_COMPILER_VERSION}. "
            "This project uses std::format, which requires at least g++-13. "
            "Compilation may fail."
        )
    endif()
endif()

###############################################################################
# Enable asserts in RelWithDebInfo mode
###############################################################################

string(REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")

###############################################################################
# Setting some complation flags
###############################################################################

set(GLOBAL_COMPILATION_FLAGS
    -Wall
    -Wextra
    -Werror
    -Wfatal-errors
    -Wno-unused-parameter
    -Wnon-virtual-dtor
    -pedantic
    -Wcast-align
    -Wmisleading-indentation
    -Wdouble-promotion
    -fno-omit-frame-pointer
    -Wshadow
    -fdiagnostics-color=always
)

if (ENABLE_ADDRESS_SANITIZER)
    message(STATUS "WARNING: Address sanitizer enabled")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# Adding compilation flag for finding __builtin_ia32_crc32si
add_compile_options(-msse4.2)

# For the vectorized code, we enable AVX-512 instructions.
add_compile_options(-mavx512f -mavx512dq -march=native)

###############################################################################
# Setting output targets
###############################################################################

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

################################################################################
# Sanity check - Disallow building in source.
# Otherwise we would overwrite the Makefiles of the old build system.
################################################################################

if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(FATAL_ERROR "In source builds are not allowed. You should invoke "
            "CMake from a different directory.")
endif()

###############################################################################
# Build the libraries
###############################################################################

add_subdirectory(libraries/libnet)

###############################################################################
# Build executables
###############################################################################

file(GLOB_RECURSE BENCH ${PROJECT_SOURCE_DIR}/bench/*.cpp)

foreach(BENCH ${BENCH})
    get_filename_component(TOOL_NAME ${BENCH} NAME_WE)

    set(BENCH_TARGET "${TOOL_NAME}-target")
    set(BENCH_SOURCES ${BENCH})

    add_executable(${BENCH_TARGET} ${BENCH_SOURCES})
    set_target_properties(${BENCH_TARGET} PROPERTIES OUTPUT_NAME "${TOOL_NAME}")
    
    target_include_directories(${BENCH_TARGET} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/libraries)
    
    target_link_libraries(${BENCH_TARGET} PUBLIC net)
endforeach(BENCH ${BENCH})

# For the tests only, build with debug symbols and without optimizations, to get better stack traces.
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0")

file (GLOB_RECURSE TESTS ${PROJECT_SOURCE_DIR}/tests/*.cpp)

foreach(TEST ${TESTS})
    get_filename_component(TEST_NAME ${TEST} NAME_WE)

    set(TEST_TARGET "${TEST_NAME}-target")
    set(TEST_SOURCES ${TEST})

    add_executable(${TEST_TARGET} ${TEST_SOURCES})
    set_target_properties(${TEST_TARGET} PROPERTIES OUTPUT_NAME "${TEST_NAME}")
    
    target_include_directories(${TEST_TARGET} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/libraries)
    
    target_link_libraries(${TEST_TARGET} PUBLIC net)
endforeach(TEST ${TESTS})
