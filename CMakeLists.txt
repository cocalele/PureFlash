
cmake_minimum_required(VERSION 3.1)
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

set(BUILD_ROOT_DIR ${CMAKE_BINARY_DIR})
set(CMAKE_BINARY_DIR ${CMAKE_BINARY_DIR}/bin)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})
set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR})
set(THIRDPARTY ${CMAKE_CURRENT_SOURCE_DIR}/thirdParty)
set(ZOOKEEPER_SOURCE ${THIRDPARTY}/zookeeper/zookeeper-client/zookeeper-client-c)
set(ZOOKEEPER_INC ${ZOOKEEPER_SOURCE}/include ${ZOOKEEPER_SOURCE}/generated)
set(ZOOKEEPER_BIN ${ZOOKEEPER_SOURCE}/.libs)

set(ZOOKEEPER_PREBUILDED 1)

execute_process(COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/osname.sh   OUTPUT_VARIABLE  OS_NAME)
message("os is ${OS_NAME}")
set(PREBUILD_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/pre_build_libs/${OS_NAME})

macro(USE_ZOOKEEPER)
    if(ZOOKEEPER_PREBUILDED)
        set(ZOOKEEPER_BIN ${PREBUILD_LIB_DIR})
        include_directories(${PREBUILD_LIB_DIR})

    elseif(UNIX)
		message("Begin to compile zookeeper")
        foreach(f ${ZOOKEEPER_SOURCE}/generated/zookeeper.jute.h ${ZOOKEEPER_SOURCE}/generated/zookeeper.jute.c)
            if(NOT EXISTS "${f}")
                execute_process(COMMAND ant clean
                        WORKING_DIRECTORY ${THIRDPARTY}/zookeeper)
                execute_process(COMMAND ant compile_jute
                        WORKING_DIRECTORY ${THIRDPARTY}/zookeeper)
            endif()
        endforeach()
    endif(ZOOKEEPER_PREBUILDED)
    add_definitions(-DTHREADED)# to use zookeeper multi thread library
    include_directories(${ZOOKEEPER_INC})
    link_directories(${ZOOKEEPER_BIN})
endmacro()
macro(use_cxx11)
    if (CMAKE_VERSION VERSION_LESS "3.1")
        if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
        endif ()
    else ()
        set (CMAKE_CXX_STANDARD 17)
    endif ()
endmacro(use_cxx11)
macro(USE_ISA_L)
	message("Begin to compile isa-l_crypto")
	execute_process(COMMAND ./autogen.sh
                    WORKING_DIRECTORY ${THIRDPARTY}/isa-l_crypto)
	execute_process(COMMAND ./configure
                    WORKING_DIRECTORY ${THIRDPARTY}/isa-l_crypto)
	execute_process(COMMAND make -j 4
                    WORKING_DIRECTORY ${THIRDPARTY}/isa-l_crypto)
	execute_process(COMMAND make install
                    WORKING_DIRECTORY ${THIRDPARTY}/isa-l_crypto)
endmacro(USE_ISA_L)
macro(USE_SG3_UTILS)
	message("Begin to compile sg3_utils")
	execute_process(COMMAND ./autogen.sh
                    WORKING_DIRECTORY ${THIRDPARTY}/sg3_utils)
	execute_process(COMMAND ./configure
                    WORKING_DIRECTORY ${THIRDPARTY}/sg3_utils)
	execute_process(COMMAND make -j 4
                    WORKING_DIRECTORY ${THIRDPARTY}/sg3_utils)
	execute_process(COMMAND make install
                    WORKING_DIRECTORY ${THIRDPARTY}/sg3_utils)
endmacro(USE_SG3_UTILS)

macro(USE_SPDK)
	message("Begin to compile SPDK")
	execute_process(COMMAND git submodule update --init
                    WORKING_DIRECTORY ${THIRDPARTY}/spdk)
	execute_process(COMMAND ./configure --disable-unit-tests --disable-tests
                    WORKING_DIRECTORY ${THIRDPARTY}/spdk)
	execute_process(COMMAND make -j 4
                    WORKING_DIRECTORY ${THIRDPARTY}/spdk)
endmacro(USE_SPDK)
USE_SPDK()

INCLUDE(CheckIncludeFiles)
CHECK_INCLUDE_FILES("rdma/rdma_cma.h" HAVE_RDMA_H)

IF (HAVE_RDMA_H)
    add_definitions(-DWITH_RDMA)
ENDIF (HAVE_RDMA_H)


USE_ZOOKEEPER()
USE_ISA_L()
USE_SG3_UTILS()

add_compile_options (-fdiagnostics-color=always)
if(NOT ZOOKEEPER_PREBUILDED)
    add_subdirectory(${ZOOKEEPER_SOURCE})
endif()

add_subdirectory(pfs)
add_subdirectory(common)
add_custom_target(scripts ALL
	COMMAND cp -rpfu ${CMAKE_CURRENT_SOURCE_DIR}/scripts ${CMAKE_BINARY_DIR}
	COMMAND mv -f ${CMAKE_BINARY_DIR}/scripts/env.sh ${CMAKE_BINARY_DIR}/..
    COMMAND cp -rp ${CMAKE_CURRENT_SOURCE_DIR}/testing ${BUILD_ROOT_DIR}
)

if (DEFINED FIO_DIR)
    execute_process(COMMAND ./configure --pfbd-include=${CMAKE_CURRENT_SOURCE_DIR}/common/include --pfbd-lib=${CMAKE_BINARY_DIR}
            WORKING_DIRECTORY ${FIO_DIR})
#    add_custom_target(fio
#        COMMAND rm -f ./fio
#        COMMAND make
#        DEPENDS ${CMAKE_BINARY_DIR}/libs5common.a
#        WORKING_DIRECTORY ${FIO_DIR}
#    )
#    add_dependencies(fio s5common)
    add_custom_command(OUTPUT ${FIO_DIR}/fio
        COMMAND rm -f ./fio
        COMMAND make
        DEPENDS ${CMAKE_BINARY_DIR}/libs5common.a
        WORKING_DIRECTORY ${FIO_DIR}
    )
    add_custom_target(fio DEPENDS ${FIO_DIR}/fio)

else()
    message("Fio not defined and will not build. You can specify fio dir by -DFIO_DIR=<fio_source_dir>")
endif()
