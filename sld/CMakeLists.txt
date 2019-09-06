cmake_minimum_required(VERSION 2.8)
if(COMMAND cmake_policy)
  cmake_policy(SET CMP0015 NEW)
endif()

PROJECT(sld)
file(GLOB_RECURSE INCS "*.h")
INCLUDE_DIRECTORIES(include ../s5afs/include )
set (BDD_SRC 
	bdd/bdd.c
)
add_executable(bdd  ${BDD_SRC} ${INCS})

set (S5BD_SRC 
	s5bd-cli/cmd_parse.c
)
add_executable(s5bd  ${S5BD_SRC} ${INCS})

add_custom_target(TARGET ALL
COMMAND echo "==========compiling s5bd driver==============="
COMMAND make clean -C ${CMAKE_CURRENT_SOURCE_DIR}/driver
COMMAND make -C ${CMAKE_CURRENT_SOURCE_DIR}/driver
COMMAND cp -rpfu ${CMAKE_CURRENT_SOURCE_DIR}/driver/s5bd.ko ${CMAKE_BINARY_DIR}/
COMMAND echo "==========copy s5bd driver to bin============="
)

