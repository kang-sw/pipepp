cmake_minimum_required(VERSION 3.1)
project(pipepp_tests)

aux_source_directory("automated" PIPEPP_TEST_SOURCE)
add_executable(pipepp_test ${PIPEPP_TEST_SOURCE})

add_dependencies(pipepp_test pipepp_core)
target_link_libraries(pipepp_test pipepp_core fmt pipepp_gui)
target_compile_features(pipepp_test PUBLIC cxx_std_20)

add_subdirectory("gui")