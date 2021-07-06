cmake_minimum_required(VERSION 3.1)
project(pipepp_core)

file(GLOB_RECURSE PIPEPP_CORE_SOURCE "include/*" "src/*")
add_library(pipepp_core ${PIPEPP_CORE_SOURCE})
add_library(pipepp::core ALIAS pipepp_core)

target_link_libraries(pipepp_core PUBLIC kangsw::templates nlohmann_json)

target_include_directories(pipepp_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include/")
target_compile_features(pipepp_core PUBLIC cxx_std_20)