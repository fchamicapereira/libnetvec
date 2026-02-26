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

file(GLOB_RECURSE TOOLS ${PROJECT_SOURCE_DIR}/tools/*.cpp)

foreach(TOOL ${TOOLS})
    get_filename_component(TOOL_NAME ${TOOL} NAME_WE)

    set(TOOL_TARGET "${TOOL_NAME}-target")
    set(TOOL_SOURCES ${TOOL})

    add_executable(${TOOL_TARGET} ${TOOL_SOURCES})
    set_target_properties(${TOOL_TARGET} PROPERTIES OUTPUT_NAME "${TOOL_NAME}")
    
    target_include_directories(${TOOL_TARGET} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/libraries)
    
    target_link_libraries(${TOOL_TARGET} PUBLIC net)
endforeach(TOOL ${TOOLS})
