
cmake_minimum_required(VERSION 2.8)
if(COMMAND cmake_policy)
  cmake_policy(SET CMP0015 NEW)
endif()

project(S5Common)
set(CMAKE_C_FLAGS   "-Wall -Wno-sign-compare -Wconversion -std=c99 -I/usr/include ${CXX_FLAG_GCOV}")
set(CMAKE_CXX_FLAGS   "-Wall -Wno-sign-compare -std=c++11 -Wconversion -fPIC ${CXX_FLAG_GCOV}")

set(CMAKE_CXX_FLAGS_DEBUG  "-O0 -g ")
set(CMAKE_CXX_COMPILER g++)

add_definitions(-D_POSIX_C_SOURCE=200112L)

IF ("${CMAKE_SIZEOF_VOID_P}" EQUAL "4")
  add_definitions(-D_OS_32)
ELSEIF("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
  add_definitions(-D_OS_64)
ENDIF()

INCLUDE_DIRECTORIES(include)
include_directories(../S5bd/include)
USE_ZOOKEEPER()

set (S5COMMON_SRC 
	src/cmdopt.c
    src/md5.c
	src/s5_buffer.cpp
	src/s5_event_queue.cpp
	src/s5_event_thread.cpp
	src/s5_poller.cpp
	src/s5_tcp_connection.cpp
	src/s5_utils.cpp
	src/s5conf.cpp
	src/s5conf_utils.cpp
	src/s5errno.cpp
	src/s5list.cpp
	src/s5message.cpp
	src/s5strtol.cpp
	src/s5utf8.cpp
	src/s5_app_ctx.cpp
	src/s5_buffer.cpp
	src/s5_event_queue.cpp
	src/s5_event_thread.cpp
	src/s5_poller.cpp
	src/s5_connection.cpp
    src/s5_zk_client.cpp
	src/s5_client_api.cpp
	)
file(GLOB_RECURSE INCS "*.h")

#set_source_files_properties(src/sqlite3/sqlite3.c PROPERTIES COMPILE_FLAGS "-w")
#set (S5SQLITE3_SRC src/sqlite3/sqlite3.c src/sqlite3/s5_sql.c)

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
TARGET_LINK_LIBRARIES(s5common dl pthread  ${GCOV}) 


