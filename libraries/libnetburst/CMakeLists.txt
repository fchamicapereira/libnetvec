################################################################################
# netburst library
################################################################################

file(GLOB_RECURSE SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_library(netburst SHARED ${SOURCES})
set_target_properties(netburst PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_options(netburst PRIVATE ${GLOBAL_COMPILATION_FLAGS})
target_compile_options(netburst PRIVATE -mavx512f -mavx512dq -march=native)
