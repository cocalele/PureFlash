
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
set(SPDKLIBS spdk_nvme spdk_env_dpdk spdk_util spdk_log spdk_sock spdk_trace spdk_json spdk_jsonrpc spdk_rpc)
set(DPDKLIBS rte_eal rte_mempool rte_ring rte_telemetry rte_kvargs rte_pci rte_bus_pci rte_mempool_ring)

add_definitions(-D_POSIX_C_SOURCE=200112L)

IF ("${CMAKE_SIZEOF_VOID_P}" EQUAL "4")
  add_definitions(-D_OS_32)
ELSEIF("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
  add_definitions(-D_OS_64)
ENDIF()

INCLUDE_DIRECTORIES(include)
include_directories(../S5bd/include ../pfs/nlohmann_json/include)
include_directories(../thirdParty/spdk/include)
include_directories(../thirdParty/spdk)
include_directories(../thirdParty/spdk/dpdk/build/include)
USE_ZOOKEEPER()

execute_process(COMMAND git log --pretty=format:'%h' -n 1
                OUTPUT_VARIABLE GIT_REV
                ERROR_QUIET)
SET_SOURCE_FILES_PROPERTIES(src/pf_utils.cpp PROPERTIES COMPILE_FLAGS -D_GIT_REV=${GIT_REV})
#add_custom_command(
#  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/git_ver.c
#  COMMAND sh  -c  "echo char *_GIT_VER= \"`git log --pretty=format:'%h' -n 1`\";" > ${CMAKE_CURRENT_BINARY_DIR}/git_ver.h
#  
#  VERBATIM)

set (S5COMMON_SRC 
	src/pf_cmdopt.c
    src/pf_md5.c
	src/pf_buffer.cpp
	src/pf_event_queue.cpp
	src/pf_event_thread.cpp
	src/pf_poller.cpp
	src/pf_tcp_connection.cpp
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
	src/pf_spdk_ring.cpp
	src/pf_poller.cpp
	src/pf_connection.cpp
	src/pf_zk_client.cpp
	src/pf_client_api.cpp
	src/pf_connection_pool.cpp
	src/pf_aof.cpp
	src/pf_aof_cache.cpp
	src/pf_ioengine.cpp
	src/pf_client_store.cpp
	#${CMAKE_CURRENT_BINARY_DIR}/git_ver.c
	)
IF (HAVE_RDMA_H)
	set (S5COMMON_SRC ${S5COMMON_SRC}  	src/pf_rdma_connection.cpp)
ENDIF (HAVE_RDMA_H)

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

link_directories(${THIRDPARTY}/spdk/build/lib)
link_directories(${THIRDPARTY}/spdk/dpdk/build/lib)


add_definitions(-D__WITH_MURMUR)
add_definitions(-DPF_CLIENT)
set (S5HASH_SRC src/hash/murmur.c src/hash/hashtable.c)
set_source_files_properties(${S5HASH_SRC} PROPERTIES COMPILE_FLAGS "-w")



add_library(s5common STATIC  ${S5COMMON_SRC} ${INCS})
set_target_properties(s5common PROPERTIES OUTPUT_NAME "s5common")
set_target_properties(s5common PROPERTIES SKIP_BUILD_RPATH true)
set_target_properties(s5common PROPERTIES CLEAN_DIRECT_OUTPUT 1)
TARGET_LINK_LIBRARIES(s5common dl rdmacm ibverbs pthread  ${GCOV})

add_executable(pfdd ${PFDD_SRC})

TARGET_LINK_LIBRARIES(pfdd s5common rdmacm ibverbs ${SPDKLIBS} ${DPDKLIBS} pthread uuid zookeeper_mt hashtable curl aio ${GCOV})

add_executable(aof_helper src/pf_aof_test_helper.cpp )
TARGET_LINK_LIBRARIES(aof_helper s5common rdmacm ${SPDKLIBS} ${DPDKLIBS} ibverbs pthread uuid zookeeper_mt hashtable curl aio ${GCOV})
add_custom_command(TARGET s5common POST_BUILD 
	COMMAND cp -rpfu ${PREBUILD_LIB_DIR}/*.a ${LIBRARY_OUTPUT_PATH}/
	COMMAND cp -rpfu ${THIRDPARTY}/spdk/build/lib/libspdk_rpc.so*  ${LIBRARY_OUTPUT_PATH}/
	COMMAND cp -rpfu ${THIRDPARTY}/spdk/build/lib/libspdk_nvme.so*  ${LIBRARY_OUTPUT_PATH}/
	COMMAND cp -rpfu ${THIRDPARTY}/spdk/build/lib/libspdk_env_dpdk.so*  ${LIBRARY_OUTPUT_PATH}/
	COMMAND cp -rpfu ${THIRDPARTY}/spdk/build/lib/libspdk_util.so*  ${LIBRARY_OUTPUT_PATH}/
	COMMAND cp -rpfu ${THIRDPARTY}/spdk/build/lib/libspdk_log.so*  ${LIBRARY_OUTPUT_PATH}/
	COMMAND cp -rpfu ${THIRDPARTY}/spdk/build/lib/libspdk_sock.so*  ${LIBRARY_OUTPUT_PATH}/
	COMMAND cp -rpfu ${THIRDPARTY}/spdk/build/lib/libspdk_trace.so*  ${LIBRARY_OUTPUT_PATH}/
	COMMAND cp -rpfu ${THIRDPARTY}/spdk/build/lib/libspdk_json.so*  ${LIBRARY_OUTPUT_PATH}/
	COMMAND cp -rpfu ${THIRDPARTY}/spdk/build/lib/libspdk_jsonrpc.so*  ${LIBRARY_OUTPUT_PATH}/
	COMMAND cp -rpfu ${THIRDPARTY}/spdk/build/lib/libspdk_vfio_user.so*  ${LIBRARY_OUTPUT_PATH}/
	COMMAND cp -rpfu ${THIRDPARTY}/spdk/dpdk/build/lib/librte_eal.so*  ${LIBRARY_OUTPUT_PATH}/
	COMMAND cp -rpfu ${THIRDPARTY}/spdk/dpdk/build/lib/librte_mempool.so*  ${LIBRARY_OUTPUT_PATH}/
	COMMAND cp -rpfu ${THIRDPARTY}/spdk/dpdk/build/lib/librte_ring.so*  ${LIBRARY_OUTPUT_PATH}/
	COMMAND cp -rpfu ${THIRDPARTY}/spdk/dpdk/build/lib/librte_telemetry.so*  ${LIBRARY_OUTPUT_PATH}/
	COMMAND cp -rpfu ${THIRDPARTY}/spdk/dpdk/build/lib/librte_kvargs.so*  ${LIBRARY_OUTPUT_PATH}/
	COMMAND cp -rpfu ${THIRDPARTY}/spdk/dpdk/build/lib/librte_pci.so*  ${LIBRARY_OUTPUT_PATH}/
	COMMAND cp -rpfu ${THIRDPARTY}/spdk/dpdk/build/lib/librte_bus_pci.so*  ${LIBRARY_OUTPUT_PATH}/
	COMMAND cp -rpfu ${THIRDPARTY}/spdk/dpdk/build/lib/librte_mempool_ring.so*  ${LIBRARY_OUTPUT_PATH}/
	COMMAND cp -rpfu ${THIRDPARTY}/spdk/dpdk/build/lib/dpdk ${LIBRARY_OUTPUT_PATH}/
)
