project(pipepp_gui_sample)

aux_source_directory("." PROJECT_SOURCES)
add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})
add_dependencies(${PROJECT_NAME} pipepp_core)
target_link_libraries(${PROJECT_NAME} pipepp_core pipepp_gui)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
