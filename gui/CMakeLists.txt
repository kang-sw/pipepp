cmake_minimum_required(VERSION 3.1)
project(pipepp_gui)

file(GLOB_RECURSE PIPEPP_GUI_SOURCE "include/*" "src/*")

add_library(				pipepp_gui	STATIC		${PIPEPP_GUI_SOURCE})
add_dependencies(			pipepp_gui				pipepp_core 
													nana)
target_link_libraries(		pipepp_gui	PUBLIC		pipepp_core 
													nana)
target_link_libraries(		pipepp_gui	PRIVATE		fmt)
target_include_directories(	pipepp_gui	PUBLIC		"include/")
target_compile_features(	pipepp_gui	PUBLIC		cxx_std_20)