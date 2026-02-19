################################################################################
# netvec library
################################################################################

file(GLOB_RECURSE SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_library(netvec SHARED ${SOURCES})
set_target_properties(netvec PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_options(netvec PRIVATE ${GLOBAL_COMPILATION_FLAGS})
target_compile_options(netvec PRIVATE -mavx512f -mavx512dq -march=native)
