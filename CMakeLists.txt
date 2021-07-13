cmake_minimum_required(VERSION 3.1)
project(pipepp)

set(PIPEPP_CORE_ROOT "${CMAKE_SOURCE_DIR}/core")

add_subdirectory(third/templates EXCLUDE_FROM_ALL)
add_subdirectory(third/json EXCLUDE_FROM_ALL)
add_subdirectory(third/nana EXCLUDE_FROM_ALL)

add_subdirectory(core)
add_subdirectory(gui)
add_subdirectory(tests)