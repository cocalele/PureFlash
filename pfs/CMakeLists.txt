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
set(CMAKE_C_FLAGS   "-Wall -Wno-conversion -Wno-sign-compare -std=c99 -fms-extensions -Wno-variadic-macros -Wno-format-truncation -I/usr/include ${C_FLAG_GCOV}")
set(CMAKE_CXX_FLAGS   "-Wall -Wconversion -Wno-sign-compare  -fms-extensions -Wno-variadic-macros -Wno-format-truncation -I/usr/include -D_XOPEN_SOURCE ${C_FLAG_GCOV}")
set(CMAKE_C_FLAGS_DEBUG  "-O0 -g -DDEBUG ")
set(CMAKE_CXX_FLAGS_DEBUG  "-O0 -g -DDEBUG ")
set(SPDKLIBS spdk_nvme spdk_env_dpdk spdk_util spdk_log spdk_sock spdk_trace spdk_json spdk_jsonrpc spdk_rpc spdk_vfio_user)
set(DPDKLIBS rte_eal rte_mempool rte_ring rte_telemetry rte_kvargs rte_pci rte_bus_pci rte_mempool_ring)
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

#INCLUDE_DIRECTORIES(include ../sld/include ../common/include ../thirdParty/mongoose nlohmann_json/include ${THIRDPARTY}/isa-l_crypto ${THIRDPARTY}/isa-l_crypto/include)
INCLUDE_DIRECTORIES(include ../sld/include ../common/include ../thirdParty/mongoose nlohmann_json/include )
include_directories(../thirdParty/spdk/include)
include_directories(../thirdParty/spdk)

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
	src/pf_bitmap.cpp
	src/pf_bgtask_manager.cpp
	src/pf_scrub.cpp
	src/pf_atslock.cpp
	src/pf_spdk_engine.cpp
	
)
IF(WITH_IOURING)
	set (S5AFS_SRC  ${S5AFS_SRC} src/pf_iouring_engine.cpp)
ENDIF(WITH_IOURING)

set (HTTP_SRC
	../thirdParty/mongoose/mongoose.c
        include/pf_threadpool.h)
IF (HAVE_RDMA_H)
	set (S5AFS_SRC ${S5AFS_SRC}  src/pf_rdma_server.cpp )
ENDIF (HAVE_RDMA_H)

file(GLOB_RECURSE INCS "*.h")
file(GLOB INCS1  "../../rdma-include/rdma/*.h")
file(GLOB INCS2  "../../rdma-include/infiniband/*.h")

link_directories(${CMAKE_BINARY_DIR}/bin)
link_directories(${THIRDPARTY}/spdk/build/lib)
link_directories(${THIRDPARTY}/spdk/dpdk/build/lib)

add_executable(pfs  ${S5AFS_SRC} ${HTTP_SRC} ${INCS})
set_target_properties(pfs PROPERTIES SKIP_BUILD_RPATH true)
#find_library(ZOOKEEPER_LIB NAMES libzookeeper_mt.a zookeeper_mt)

TARGET_LINK_LIBRARIES(pfs rdmacm ibverbs pthread zookeeper_mt  hashtable uuid ${SPDKLIBS} ${DPDKLIBS} s5common nlohmann_json::nlohmann_json
	aio curl  ${THIRDPARTY}/isa-l_crypto/.libs/libisal_crypto.a libsgutils2.a)
IF(WITH_IOURING)
	TARGET_LINK_LIBRARIES(pfs  uring)
ENDIF(WITH_IOURING)

add_custom_command(TARGET pfs POST_BUILD 
	COMMAND cp -rpfu ${CMAKE_CURRENT_SOURCE_DIR}/pfs_template.conf ${CMAKE_BINARY_DIR}
)

