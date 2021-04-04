
cmake_minimum_required(VERSION 2.8)
if(COMMAND cmake_policy)
  cmake_policy(SET CMP0015 NEW)
endif()
use_cxx11()

project(S5Common)
set(CMAKE_C_FLAGS   "-Wall -Wno-sign-compare -Wconversion -std=c99 -I/usr/include ${CXX_FLAG_GCOV}")
set(CMAKE_CXX_FLAGS   "-Wall -Wno-sign-compare -Wconversion -fPIC ${CXX_FLAG_GCOV}")

set(CMAKE_CXX_FLAGS_DEBUG  "-O0 -g ")
set(CMAKE_CXX_COMPILER g++)

add_definitions(-D_POSIX_C_SOURCE=200112L)

IF ("${CMAKE_SIZEOF_VOID_P}" EQUAL "4")
  add_definitions(-D_OS_32)
ELSEIF("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
  add_definitions(-D_OS_64)
ENDIF()

INCLUDE_DIRECTORIES(include)
include_directories(../S5bd/include ../pfs/nlohmann_json/include)
USE_ZOOKEEPER()

set (S5COMMON_SRC 
	src/pf_cmdopt.c
    src/pf_md5.c
	src/pf_buffer.cpp
	src/pf_event_queue.cpp
	src/pf_event_thread.cpp
	src/pf_poller.cpp
	src/pf_tcp_connection.cpp
	src/pf_rdma_connection.cpp
	src/pf_utils.cpp
	src/pf_conf.cpp
	src/pf_conf_utils.cpp
	src/pf_errno.cpp
	src/pf_list.cpp
	src/pf_message.cpp
	src/pf_strtol.cpp
	src/pf_utf8.cpp
	src/pf_app_ctx.cpp
	src/pf_buffer.cpp
	src/pf_event_queue.cpp
	src/pf_event_thread.cpp
	src/pf_poller.cpp
	src/pf_connection.cpp
	src/pf_zk_client.cpp
	src/pf_client_api.cpp
	src/pf_connection_pool.cpp
	)
set (PFDD_SRC
		src/pf_pfdd.cpp
	)
file(GLOB_RECURSE INCS "*.h")
file(GLOB INCS1  "../../rdma-include/rdma/*.h")
file(GLOB INCS2  "../../rdma-include/infiniband/*.h")

#set_source_files_properties(src/sqlite3/sqlite3.c PROPERTIES COMPILE_FLAGS "-w")
#set (S5SQLITE3_SRC src/sqlite3/sqlite3.c src/sqlite3/pf_sql.c)

#ADD_LIBRARY(s5sqlite3 SHARED ${S5SQLITE3_SRC}) 
#set_target_properties(s5sqlite3 PROPERTIES SKIP_BUILD_RPATH true)
#set_target_properties(s5sqlite3 PROPERTIES CLEAN_DIRECT_OUTPUT 1)


add_definitions(-D__WITH_MURMUR)
set (S5HASH_SRC src/hash/murmur.c src/hash/hashtable.c)
set_source_files_properties(${S5HASH_SRC} PROPERTIES COMPILE_FLAGS "-w")



add_library(s5common STATIC  ${S5COMMON_SRC} ${INCS})
set_target_properties(s5common PROPERTIES OUTPUT_NAME "s5common")
set_target_properties(s5common PROPERTIES SKIP_BUILD_RPATH true)
set_target_properties(s5common PROPERTIES CLEAN_DIRECT_OUTPUT 1)
TARGET_LINK_LIBRARIES(s5common dl rdmacm ibverbs pthread  ${GCOV})

add_executable(pfdd ${PFDD_SRC})
TARGET_LINK_LIBRARIES(pfdd s5common rdmacm ibverbs pthread uuid zookeeper curl ${GCOV})
