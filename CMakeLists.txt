cmake_minimum_required(VERSION 3.1)
project(pipepp)

set(PIPEPP_CORE_ROOT "${CMAKE_SOURCE_DIR}/core")

add_subdirectory(third/templates)
add_subdirectory(third/fmt)
add_subdirectory(third/json)

add_subdirectory(core)

add_subdirectory(tests)