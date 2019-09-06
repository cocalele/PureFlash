cmake_minimum_required(VERSION 2.8)
if(COMMAND cmake_policy)
  cmake_policy(SET CMP0015 NEW)
endif()

PROJECT(s5bd)


set(S5COMMON_SRC ${s5bd_SOURCE_DIR}/../Common)
set(S5MANAGER_SRC ${s5bd_SOURCE_DIR}/../S5manager)

set(CMAKE_C_FLAGS   "-Wall -Wconversion -fPIC -I/usr/include ${C_FLAG_GCOV}")
#message ("\n ***** C_FLAG_GCV=${C_FLAG_GCOV} *****\n")
set(CMAKE_C_FLAGS_DEBUG  "-O0 -g ")

set(CMAKE_CXX_FLAGS   "-Wall -Wconversion -fPIC ${CXX_FLAG_GCOV}")
#message ("\n ***** C_FLAG_GCV=${CXX_FLAG_GCOV} *****\n")
set(CMAKE_CXX_FLAGS_DEBUG  "-O0 -g ")
set(CMAKE_CXX_COMPILER g++)

include_directories(${s5bd_SOURCE_DIR}/include)
include_directories(${S5COMMON_SRC}/include)

link_directories(${S5COMMON_SRC}/lib)
link_directories(/usr/local/lib)

#add_subdirectory(unittest)
file(GLOB_RECURSE INCS "*.h")

	
add_library(s5bd SHARED 
	src/libs5bd.c 
	src/s5imagectx.c 
	src/s5session.c
	src/s5aiocompletion.c 
	src/tasknode.c
	src/idgenerator.c
	src/bitarray.c	
	src/s5_context.c
	src/disable_warn.c
	${INCS})

target_link_libraries(s5bd s5common pthread log4c ${GCOV})
#message ("\n ***** GCOV=${GCOV} *****\n")
set_target_properties(s5bd PROPERTIES SKIP_BUILD_RPATH true)

add_subdirectory(unittest)

