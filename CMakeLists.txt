
cmake_minimum_required(VERSION 2.8)
if(COMMAND cmake_policy)
  cmake_policy(SET CMP0015 NEW)
endif()

project(PureFlash)
if(LINUX)
find_program(ANT ant)
if(NOT ANT)
    message(FATAL_ERROR "ant not found! need it to build zookeeper c client")
endif()
endif(LINUX)

set(CMAKE_BINARY_DIR ${CMAKE_BINARY_DIR}/bin)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})
set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR})
set(THIRDPARTY ${CMAKE_CURRENT_SOURCE_DIR}/thirdParty)
set(ZOOKEEPER_SOURCE ${THIRDPARTY}/zookeeper/zookeeper-client/zookeeper-client-c)
set(ZOOKEEPER_INC ${ZOOKEEPER_SOURCE}/include ${ZOOKEEPER_SOURCE}/generated)
#set(ZOOKEEPER_BIN ${THIRDPARTY}/zookeeper-3.4.6/src/c/.libs)

macro(USE_ZOOKEEPER)
    if(UNIX)
    foreach(f ${ZOOKEEPER_SOURCE}/generated/zookeeper.jute.h ${ZOOKEEPER_SOURCE}/generated/zookeeper.jute.c)
        if(NOT EXISTS "${f}")
            execute_process(COMMAND ant clean
                    WORKING_DIRECTORY ${THIRDPARTY}/zookeeper)
            execute_process(COMMAND ant compile_jute
                    WORKING_DIRECTORY ${THIRDPARTY}/zookeeper)
        endif()
    endforeach()
    endif(UNIX)
    add_definitions(-DTHREADED)# to use zookeeper multi thread library
    include_directories(${ZOOKEEPER_INC})
    #link_directories(${ZOOKEEPER_BIN})
endmacro()
macro(use_cxx11)
    if (CMAKE_VERSION VERSION_LESS "3.1")
        if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11")
        endif ()
    else ()
        set (CMAKE_CXX_STANDARD 11)
    endif ()
endmacro(use_cxx11)

USE_ZOOKEEPER()
add_compile_options (-fdiagnostics-color=always)
add_subdirectory(${ZOOKEEPER_SOURCE})

add_subdirectory(pfs)
add_subdirectory(common)
add_custom_target(scripts ALL
	COMMAND cp -rpfu ${CMAKE_CURRENT_SOURCE_DIR}/scripts ${CMAKE_BINARY_DIR}
#    COMMAND cp -rpfu ${ZOOKEEPER_SOURCE}/.libs/libzookeeper_mt.a ${CMAKE_BINARY_DIR}
	COMMAND mv -f ${CMAKE_BINARY_DIR}/scripts/env.sh ${CMAKE_BINARY_DIR}/..
)
