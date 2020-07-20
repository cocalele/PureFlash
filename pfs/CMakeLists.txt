cmake_minimum_required(VERSION 2.8)
if(COMMAND cmake_policy)
  cmake_policy(SET CMP0015 NEW)
endif()
use_cxx11()
project(pfs)

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

set(CMAKE_C_FLAGS   "-Wall -Wno-conversion -Wno-sign-compare -std=c99 -fms-extensions -Wno-variadic-macros -I/usr/include ${C_FLAG_GCOV}")
set(CMAKE_CXX_FLAGS   "-Wall -Wconversion -Wno-sign-compare  -fms-extensions -Wno-variadic-macros -I/usr/include -D_XOPEN_SOURCE ${C_FLAG_GCOV}")
set(CMAKE_C_FLAGS_DEBUG  "-O0 -g -DDEBUG ")
set(CMAKE_CXX_FLAGS_DEBUG  "-O0 -g -DDEBUG ")
add_definitions(-D_XOPEN_SOURCE)
#set(CMAKE_CXX_COMPILER g++)


# Typically you don't care so much for a third party library's tests to be
# run from your own project's code.
set(JSON_BuildTests OFF CACHE INTERNAL "")

# If you only include this third party in PRIVATE source files, you do not
# need to install it when your main project gets installed.
# set(JSON_Install OFF CACHE INTERNAL "")

# Don't use include(nlohmann_json/CMakeLists.txt) since that carries with it
# unintended consequences that will break the build.  It's generally
# discouraged (although not necessarily well documented as such) to use
# include(...) for pulling in other CMake projects anyways.
add_subdirectory(nlohmann_json)

INCLUDE_DIRECTORIES(include ../sld/include ../common/include ../thirdParty/mongoose nlohmann_json/include)

set (S5AFS_SRC 
	src/pf_cluster.cpp
	src/pf_flash_store.cpp
	src/pf_main.cpp
	src/pf_s5message.cpp
	src/pf_server.cpp
	src/pf_dispatcher.cpp
	src/pf_md5.cpp
	src/pf_redolog.cpp
	src/pf_block_tray.cpp
	src/pf_replica.cpp
	src/pf_restful_server.cpp
	src/pf_restful_api.cpp
	src/pf_volume.cpp
	src/pf_error_handler.cpp
	src/pf_replicator.cpp
		)
set (HTTP_SRC
	../thirdParty/mongoose/mongoose.c
    )
	
file(GLOB_RECURSE INCS "*.h")

add_executable(pfs  ${S5AFS_SRC} ${HTTP_SRC} ${INCS})
set_target_properties(pfs PROPERTIES SKIP_BUILD_RPATH true)

TARGET_LINK_LIBRARIES(pfs pthread zookeeper uuid s5common nlohmann_json::nlohmann_json aio ${GCOV})
add_custom_command(TARGET pfs POST_BUILD 
        #COMMAND rm -f ${LIBRARY_OUTPUT_PATH}/libzookeeper_mt.so.2.0.0 ${LIBRARY_OUTPUT_PATH}/libzookeeper_mt.so.2 ${LIBRARY_OUTPUT_PATH}/libzookeeper_mt.so
	COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/pfs_template.conf ${CMAKE_BINARY_DIR}
        #COMMAND cp ${ZOOKEEPER_BIN}/libzookeeper_mt.so.2.0.0 ${LIBRARY_OUTPUT_PATH}/libzookeeper_mt.so.2.0.0
        #COMMAND ln -s libzookeeper_mt.so.2.0.0 ${LIBRARY_OUTPUT_PATH}/libzookeeper_mt.so.2
        #COMMAND ln -s libzookeeper_mt.so.2.0.0 ${LIBRARY_OUTPUT_PATH}/libzookeeper_mt.so
	DEPENDS ${ZOOKEEPER_BIN}/libzookeeper_mt.so.2.0.0 
)

