cmake_minimum_required(VERSION 3.25)
project(keepalive LANGUAGES C)

include(CheckFunctionExists)
include(CheckSymbolExists)

MACRO(REPLACE_FUNCTIONS sources)
	FOREACH(name ${ARGN})
		STRING(TOUPPER have_${name} SYMBOL_NAME)
		CHECK_FUNCTION_EXISTS(${name} ${SYMBOL_NAME})
		IF(NOT ${SYMBOL_NAME})
			SET(${sources} ${${sources}} compat/${name}.c)
		ENDIF(NOT ${SYMBOL_NAME})
	ENDFOREACH(name)
ENDMACRO(REPLACE_FUNCTIONS)

REPLACE_FUNCTIONS(COMPAT_SOURCES explicit_bzero freezero getdtablesize 
	getdtablecount reallocarray recallocarray strtonum)

list(APPEND CMAKE_REQUIRED_LIBRARIES util)
check_symbol_exists(imsg_init "sys/queue.h;imsg.h" HAVE_IMSG)
list(REMOVE_ITEM CMAKE_REQUIRED_LIBRARIES util)

if (HAVE_IMSG)
	set(IMSG_LIBS util)
else()
	set(IMSG_SOURCES ${CMAKE_SOURCE_DIR}/compat/imsg.c ${CMAKE_SOURCE_DIR}/compat/imsg-buffer.c)
endif()

include_directories(${CMAKE_SOURCE_DIR})
include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/compat)
add_compile_options(-include config.h)

find_package(FLEX 2.5 REQUIRED)
find_package(BISON 2.5 REQUIRED) #Does not actually need bison...

bison_target(parser parse.y parse.c)
flex_target(lexer lex.l lex.c)

add_executable(keepalivec keepalivec.c)
target_sources(keepalivec PRIVATE ${COMPAT_SOURCES})

add_executable(keepalived keepalived.c lex.c parse.c)
target_link_libraries(keepalived event ${IMSG_LIBS})
target_sources(keepalived PRIVATE ${COMPAT_SOURCES} ${IMSG_SOURCES})

add_executable(keepalived-session session.c)
target_link_libraries(keepalived-session event ${IMSG_LIBS})
target_sources(keepalived-session PRIVATE ${COMPAT_SOURCES} ${IMSG_SOURCES})

configure_file(config.h.in config.h)
