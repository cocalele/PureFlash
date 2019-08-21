USE_S5LOG()
USE_GTEST()
include_directories(${s5bd_SOURCE_DIR}/include)
include_directories(${S5COMMON_SRC}/include)
include_directories(${CEPH_SRC}/src)
link_directories(${S5COMMON_SRC}/lib)
link_directories(${CEPH_SRC}/src/.libs)
link_directories(/usr/local/lib)

#add_executable(test_libs5bd test_libs5bd.cpp)
#set_target_properties(test_libs5bd PROPERTIES SKIP_BUILD_RPATH true)
#target_link_libraries(test_libs5bd s5bd s5common gtest log4c)

#add_executable(test_threadpool test_threadpool.cpp)
#set_target_properties(test_threadpool PROPERTIES SKIP_BUILD_RPATH true)
#target_link_libraries(test_threadpool s5bd gtest log4c)

#add_executable(test_memory_item test_memory_item.cpp)
#set_target_properties(test_memory_item PROPERTIES SKIP_BUILD_RPATH true)
#target_link_libraries(test_memory_item s5bd gtest log4c)

#add_executable(performance_test performance_test.cpp)
#set_target_properties(performance_test PROPERTIES SKIP_BUILD_RPATH true)
#target_link_libraries(performance_test s5bd)

#add_executable(line_up_test line_up_test.cpp)
#set_target_properties(line_up_test PROPERTIES SKIP_BUILD_RPATH true)
#target_link_libraries(line_up_test s5bd)

add_executable(s5bd_api_unittest libs5bd_api.cpp)
set_target_properties(s5bd_api_unittest PROPERTIES SKIP_BUILD_RPATH true)
target_link_libraries(s5bd_api_unittest s5bd s5common gtest log4c s5manager)
add_custom_target(test_data ALL
	COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/import_test.data ${CMAKE_BINARY_DIR}
	)

add_executable(s5bd_memleak_unittest test_memory_leak.cpp rt_info.c)
set_target_properties(s5bd_memleak_unittest PROPERTIES SKIP_BUILD_RPATH true)
target_link_libraries(s5bd_memleak_unittest s5bd s5common gtest log4c s5manager)

