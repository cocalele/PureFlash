
cmake_minimum_required(VERSION 2.8)
if(COMMAND cmake_policy)
  cmake_policy(SET CMP0015 NEW)
endif()

project(PureFlash)

find_program(ANT ant)
if(NOT ANT)
    message(FATAL_ERROR "ant not found! need it to build zookeeper c client")
endif()

set(CMAKE_BINARY_DIR ${CMAKE_BINARY_DIR}/bin)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})
set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR})

add_compile_options (-fdiagnostics-color=always)
add_subdirectory(s5afs)
add_subdirectory(Common)
add_custom_target(scripts ALL
	COMMAND cp -rpfu ${CMAKE_CURRENT_SOURCE_DIR}/scripts ${CMAKE_BINARY_DIR}
	COMMAND mv -f ${CMAKE_BINARY_DIR}/scripts/env.sh ${CMAKE_BINARY_DIR}/..
)
