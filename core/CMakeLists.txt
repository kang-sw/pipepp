cmake_minimum_required(VERSION 3.1)
project(pipepp_core)

file(GLOB_RECURSE PIPEPP_CORE_SOURCE "include/*" "src/*")
add_library(pipepp_core STATIC ${PIPEPP_CORE_SOURCE})

add_dependencies(pipepp_core fmt)
target_link_libraries(pipepp_core PUBLIC kangsw_templates nlohmann_json)
target_link_libraries(pipepp_core PRIVATE fmt)

target_include_directories(pipepp_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include/")
target_compile_features(pipepp_core PUBLIC cxx_std_20)