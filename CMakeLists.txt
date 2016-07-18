cmake_minimum_required(VERSION 2.8.11)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake-scripts)
include(generic)

include(FindPkgConfig)
if (PKG_CONFIG_FOUND)
	pkg_check_modules(LIBUNWIND REQUIRED libunwind)
	if(NOT CMAKE_BUILD_TYPE MATCHES Debug)
		pkg_check_modules(LIBTCMALLOC_MINIMAL libtcmalloc_minimal)
		if(LIBTCMALLOC_MINIMAL_FOUND EQUAL 1)
			set(GB_LD_EXTRA "${LIBTCMALLOC_MINIMAL_LIBRARIES}")
		endif (LIBTCMALLOC_MINIMAL_FOUND EQUAL 1)
	endif(NOT CMAKE_BUILD_TYPE MATCHES Debug)
endif (PKG_CONFIG_FOUND)

add_subdirectory(contrib/pfcq)

add_executable(pingtcp
	pingtcp.c)

target_link_libraries(pingtcp
	pthread
	dl
	m
	ln_pfcq
	${LIBUNWIND_LIBRARIES}
	${GB_LD_EXTRA})

install(TARGETS pingtcp
	RUNTIME DESTINATION bin)

