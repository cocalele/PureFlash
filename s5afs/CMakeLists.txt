cmake_minimum_required(VERSION 2.8)
if(COMMAND cmake_policy)
  cmake_policy(SET CMP0015 NEW)
endif()

project(s5afs)

if(${CMAKE_BUILD_TYPE} MATCHES "Debug")
    add_definitions(-DDEBUG)
elseif(${CMAKE_BUILD_TYPE} MATCHES "Release")
endif()

IF(${ENABLE_GCOV} MATCHES "YES")
   SET(C_FLAG_GCOV "-fprofile-arcs -ftest-coverage")
   SET(CXX_FLAG_GCOV "-fprofile-arcs -ftest-coverage")
   SET(GCOV gcov)
ELSE()
   SET(C_FLAG_GCOV "")
   SET(CXX_FLAG_GCOV "")
   SET(GCOV "")
ENDIF()
set(THIRDPARTY ${CMAKE_CURRENT_SOURCE_DIR}/thirdParty)
set(LOG4C_INC ${THIRDPARTY}/log4c-1.2.4/src)
set(LOG4C_BIN ${THIRDPARTY}/log4c-1.2.4/bin)
macro(USE_S5LOG)
include_directories(${LOG4C_INC})
link_directories(${LOG4C_BIN})
endmacro()

USE_S5LOG()

set(CMAKE_C_FLAGS   "-Wall -Wconversion --std=c99 -fms-extensions -Wno-variadic-macros -I/usr/include ${C_FLAG_GCOV}")
set(CMAKE_C_FLAGS_DEBUG  "-O0 -g -DDEBUG -D_GLIBCXX_DEBUG")
add_definitions(-D_XOPEN_SOURCE)
set(CMAKE_CXX_FLAGS   "-Wall -Wconversion -fPIC ${C_FLAG_GCOV}")
set(CMAKE_CXX_FLAGS_DEBUG  "-O0 -g ")
set(CMAKE_CXX_COMPILER g++)

INCLUDE_DIRECTORIES(include ../sld/include )

set (S5AFS_SRC 
	src/afs_cmdopt.c
	src/afs_fixed_size_queue.c
	src/afs_conf.c
	src/afs_conf_utils.c
	src/afs_list.c
	src/afs_s5message.c
	src/afs_socket.cpp
	src/afs_socket_impl.cpp
	src/afs_strtol.c
	src/afs_utf8.c
	src/afs_utils.c
	src/afs_spy.cpp
	src/afs_hashtable.c
	src/afs_murmur.c
	src/afs_main.c 
	src/afs_server.c 
	src/afs_request.c
	src/afs_flash_store.c
)
file(GLOB_RECURSE INCS "*.h")

add_executable(s5afs  ${S5AFS_SRC} ${INCS})
add_executable(spy  src/afs_spy_client.cpp)
set_target_properties(s5afs PROPERTIES SKIP_BUILD_RPATH true)

TARGET_LINK_LIBRARIES(s5afs pthread log4c ${GCOV})
add_custom_command(TARGET s5afs POST_BUILD 
	COMMAND rm -f ${LIBRARY_OUTPUT_PATH}/liblog4c.so.3.3.1 ${LIBRARY_OUTPUT_PATH}/liblog4c.so.3 ${LIBRARY_OUTPUT_PATH}/liblog4c.so
	COMMAND cp ${LOG4C_BIN}/liblog4c.so.3.3.1 ${LIBRARY_OUTPUT_PATH}/liblog4c.so.3.3.1
	COMMAND ln -s liblog4c.so.3.3.1 ${LIBRARY_OUTPUT_PATH}/liblog4c.so.3
	COMMAND ln -s liblog4c.so.3.3.1 ${LIBRARY_OUTPUT_PATH}/liblog4c.so	
	DEPENDS ${LOG4C_BIN}/liblog4c.so.3.3.1
)