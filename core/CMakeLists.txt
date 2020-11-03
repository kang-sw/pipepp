cmake_minimum_required(VERSION 3.1)
project(pipepp_core)

file(GLOB_RECURSE PIPEPP_CORE_SOURCE "include/*" "src/*")
add_library(pipepp_core STATIC ${PIPEPP_CORE_SOURCE})

target_include_directories(pipepp_core PUBLIC "include/")
target_compile_features(pipepp_core PUBLIC cxx_std_20)