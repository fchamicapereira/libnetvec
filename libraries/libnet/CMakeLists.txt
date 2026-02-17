################################################################################
# net library
################################################################################

file(GLOB_RECURSE SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_library(net SHARED ${SOURCES})
set_target_properties(net PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_options(net PRIVATE ${GLOBAL_COMPILATION_FLAGS})

# Adding -std=gnu11 to compile options for C files, and -std=gnu++17 for C++ files
target_compile_options(net PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-std=gnu11>
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
)

# Adding compilation flag for finding __builtin_ia32_crc32si
target_compile_options(net PRIVATE -msse4.2)
