
cmake_minimum_required(VERSION 2.8)
if(COMMAND cmake_policy)
  cmake_policy(SET CMP0015 NEW)
endif()

project(S5Common)
set(CMAKE_CXX_FLAGS   "-Wall ")
set(CMAKE_CXX_FLAGS_DEBUG  "-O0 -g ")
set(CMAKE_CXX_COMPILER g++)

IF(${ENABLE_GCOV} MATCHES "YES")                                                                                                                                                             
   SET(C_FLAG_GCOV "-fprofile-arcs -ftest-coverage")
   SET(CXX_FLAG_GCOV "-fprofile-arcs -ftest-coverage")
   SET(GCOV gcov)
ELSE()
   SET(C_FLAG_GCOV "")
   SET(CXX_FLAG_GCOV "")
   SET(GCOV "")
ENDIF()

#AUX_SOURCE_DIRECTORY(../common  SRC_LIST)
INCLUDE_DIRECTORIES(../include ../../S5bd/include ../include/sqlite3 ../include/hash)
USE_GTEST()
USE_S5LOG()
USE_ZEROMQ()
USE_CZMQ()


	
set(COMMON_GTEST common_gtest.cpp common_gtest2.cpp)
set(CLT_SRC clt_socket.c )	
set(SRV_SRC srv_socket.c )	
set(T_S5LIST_SRC test_s5list.c )
set(T_WORKER_SRC test_worker.c)
set(T_CNDCT_SRC test_cndct.c)
set(TEST_ZMSG_SRC test_s5mq_msg_pack_unpack.c)
set(TEST_S5MQ_TRANS_ST  test_s5mq_trans_status_by_hb_msg.c)


add_executable(s5common_clt_socket_unittest  ${CLT_SRC})
set_target_properties(s5common_clt_socket_unittest PROPERTIES SKIP_BUILD_RPATH true)
TARGET_LINK_LIBRARIES(s5common_clt_socket_unittest pthread log4c s5common)
#TARGET_LINK_LIBRARIES(clt_socket pthread)

add_executable(s5common_srv_socket_unittest  ${SRV_SRC})
set_target_properties(s5common_srv_socket_unittest PROPERTIES SKIP_BUILD_RPATH true)
TARGET_LINK_LIBRARIES(s5common_srv_socket_unittest pthread log4c s5common)
#TARGET_LINK_LIBRARIES(srv_socket pthread)

add_executable(s5common_common_gtest_unittest  ${COMMON_GTEST})
set_target_properties(s5common_common_gtest_unittest PROPERTIES SKIP_BUILD_RPATH true)
TARGET_LINK_LIBRARIES(s5common_common_gtest_unittest pthread log4c gtest gtest_main s5common)

add_executable(s5common_conf_unittest gtest_conf.cpp)
set_target_properties(s5common_conf_unittest PROPERTIES SKIP_BUILD_RPATH true)
target_link_libraries(s5common_conf_unittest s5common gtest log4c)

add_executable(s5common_utility_unittest util_test.cpp)
set_target_properties(s5common_utility_unittest PROPERTIES SKIP_BUILD_RPATH true)
target_link_libraries(s5common_utility_unittest s5common gtest)

add_executable(test_s5list ${T_S5LIST_SRC})
set_target_properties(test_s5list PROPERTIES SKIP_BUILD_RPATH true)
TARGET_LINK_LIBRARIES(test_s5list pthread log4c s5common zmq czmq)

add_executable(test_worker ${T_WORKER_SRC})
set_target_properties(test_worker PROPERTIES SKIP_BUILD_RPATH true)
TARGET_LINK_LIBRARIES(test_worker pthread log4c s5common zmq czmq ${GCOV})

set(T_SQL_SRC test_s5sql.c)
add_executable(test_s5sql ${T_SQL_SRC})
set_target_properties(test_s5sql PROPERTIES SKIP_BUILD_RPATH true)
TARGET_LINK_LIBRARIES(test_s5sql pthread log4c s5common s5sqlite3)

add_definitions(-D__WITH_MURMUR)
set(T_HASH_SRC hash/main.c)
add_executable(test_s5hash ${T_HASH_SRC})
set_target_properties(test_s5hash PROPERTIES SKIP_BUILD_RPATH true)
TARGET_LINK_LIBRARIES(test_s5hash s5common s5hash)

add_executable(test_cndct ${T_CNDCT_SRC})
set_target_properties(test_cndct PROPERTIES SKIP_BUILD_RPATH true)
TARGET_LINK_LIBRARIES(test_cndct pthread log4c s5common zmq czmq ${GCOV})

add_executable(test_s5mq_pack_unpack ${TEST_ZMSG_SRC})
set_target_properties(test_s5mq_pack_unpack PROPERTIES SKIP_BUILD_RPATH true)
TARGET_LINK_LIBRARIES(test_s5mq_pack_unpack pthread log4c s5common zmq czmq ${GCOV})

add_executable(test_s5mq_trans_st ${TEST_S5MQ_TRANS_ST})
set_target_properties(test_s5mq_trans_st PROPERTIES SKIP_BUILD_RPATH true)
TARGET_LINK_LIBRARIES(test_s5mq_trans_st pthread log4c s5common zmq czmq)


add_custom_command(TARGET test_cndct POST_BUILD 
    COMMAND mkdir -p ${LIBRARY_OUTPUT_PATH}/unittest_conf/
    COMMAND cp -f ${UNITTEST_DIR}/s5_autotest_gcov_s5mq.sh ${LIBRARY_OUTPUT_PATH}/s5_autotest_gcov_s5mq.sh
    COMMAND cp -f ${CONF_DIR}/mq_cndct.conf ${LIBRARY_OUTPUT_PATH}/unittest_conf/mq_cndct.conf
    COMMAND cp -f ${CONF_DIR}/mq_worker.conf ${LIBRARY_OUTPUT_PATH}/unittest_conf/mq_worker.conf
    DEPENDS ${UNITTEST_DIR}/s5_autotest_gcov_s5mq.sh
)   

add_custom_command(TARGET s5common_conf_unittest POST_BUILD 
    COMMAND cp -f ${UNITTEST_DIR}/conf_file/s5unittest.conf ${LIBRARY_OUTPUT_PATH}/unittest_conf/s5unittest.conf
    DEPENDS ${UNITTEST_DIR}/conf_file/s5unittest.conf
)
