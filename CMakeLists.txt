
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
set(THIRDPARTY ${CMAKE_CURRENT_SOURCE_DIR}/thirdParty)
set(ZOOKEEPER_SOURCE ${THIRDPARTY}/zookeeper/zookeeper-client/zookeeper-client-c)
set(ZOOKEEPER_INC ${ZOOKEEPER_SOURCE}/include ${ZOOKEEPER_SOURCE}/generated)
#set(ZOOKEEPER_BIN ${THIRDPARTY}/zookeeper-3.4.6/src/c/.libs)
macro(USE_ZOOKEEPER)
    foreach(f ${ZOOKEEPER_SOURCE}/generated/zookeeper.jute.h ${ZOOKEEPER_SOURCE}/generated/zookeeper.jute.c)
        if(NOT EXISTS "${f}")
            execute_process(COMMAND ant clean
                    WORKING_DIRECTORY ${THIRDPARTY}/zookeeper)
            execute_process(COMMAND ant compile_jute
                    WORKING_DIRECTORY ${THIRDPARTY}/zookeeper)
        endif()
    endforeach()
    include_directories(${ZOOKEEPER_INC})
    #link_directories(${ZOOKEEPER_BIN})
endmacro()

add_compile_options (-fdiagnostics-color=always)
add_subdirectory(${ZOOKEEPER_SOURCE})

add_subdirectory(s5afs)
add_subdirectory(Common)
add_custom_target(scripts ALL
	COMMAND cp -rpfu ${CMAKE_CURRENT_SOURCE_DIR}/scripts ${CMAKE_BINARY_DIR}
	COMMAND mv -f ${CMAKE_BINARY_DIR}/scripts/env.sh ${CMAKE_BINARY_DIR}/..
)
