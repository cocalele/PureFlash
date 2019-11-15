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




USE_ZOOKEEPER()

set(CMAKE_C_FLAGS   "-Wall -Wconversion --std=c99 -fms-extensions -Wno-variadic-macros -I/usr/include ${C_FLAG_GCOV}")
set(CMAKE_CXX_FLAGS   "-Wall -Wconversion --std=c++11 -fms-extensions -Wno-variadic-macros -I/usr/include ${C_FLAG_GCOV}")
set(CMAKE_C_FLAGS_DEBUG  "-O0 -g -DDEBUG -D_GLIBCXX_DEBUG")
set(CMAKE_CXX_FLAGS_DEBUG  "-O0 -g -DDEBUG -D_GLIBCXX_DEBUG")
add_definitions(-D_XOPEN_SOURCE)
set(CMAKE_CXX_COMPILER g++)

INCLUDE_DIRECTORIES(include ../sld/include ../Common/include ../thirdParty/mongoose)

set (S5AFS_SRC 
	src/afs_cluster.cpp
	src/afs_flash_store.cpp
	src/afs_main.cpp
	src/afs_s5message.cpp
	src/afs_server.cpp
	src/s5_dispatcher.cpp
	src/s5_md5.cpp
	src/s5_redolog.cpp
    src/s5_block_tray.cpp
	src/s5_replica.cpp
	src/s5_restful_server.cpp
)
set (HTTP_SRC
	../thirdParty/mongoose/mongoose.c
)
	
file(GLOB_RECURSE INCS "*.h")

add_executable(s5afs  ${S5AFS_SRC} ${HTTP_SRC} ${INCS})
set_target_properties(s5afs PROPERTIES SKIP_BUILD_RPATH true)

TARGET_LINK_LIBRARIES(s5afs pthread zookeeper uuid s5common ${GCOV})
add_custom_command(TARGET s5afs POST_BUILD 
        #COMMAND rm -f ${LIBRARY_OUTPUT_PATH}/libzookeeper_mt.so.2.0.0 ${LIBRARY_OUTPUT_PATH}/libzookeeper_mt.so.2 ${LIBRARY_OUTPUT_PATH}/libzookeeper_mt.so
	COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/s5afs_template.conf ${CMAKE_BINARY_DIR}
        #COMMAND cp ${ZOOKEEPER_BIN}/libzookeeper_mt.so.2.0.0 ${LIBRARY_OUTPUT_PATH}/libzookeeper_mt.so.2.0.0
        #COMMAND ln -s libzookeeper_mt.so.2.0.0 ${LIBRARY_OUTPUT_PATH}/libzookeeper_mt.so.2
        #COMMAND ln -s libzookeeper_mt.so.2.0.0 ${LIBRARY_OUTPUT_PATH}/libzookeeper_mt.so
	DEPENDS ${ZOOKEEPER_BIN}/libzookeeper_mt.so.2.0.0 
)

