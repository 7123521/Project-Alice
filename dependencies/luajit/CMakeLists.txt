cmake_minimum_required(VERSION 3.15)

if(WIN32)
	message(STATUS "A certain luajit version is distributed with Alice")
	add_library(lib_luajit INTERFACE)
	target_link_libraries(lib_luajit INTERFACE ${PROJECT_SOURCE_DIR}/libs/luajit.lib)
else()
	# Link the system LuaJIT and use the headers vendored in src/scripting/luajit.
	#
	# Building LuaJIT here with ExternalProject produced a library whose SONAME
	# (libluajit-5.1.so.2) lives only inside the build tree, so the compiled binary
	# failed to start outside of it: "libluajit-5.1.so.2 => not found". Linking the
	# distro package instead keeps the binary runnable on the target machine and
	# removes a network fetch from configure.
	find_library(LUAJIT_LIBRARY NAMES luajit-5.1 luajit)
	if(NOT LUAJIT_LIBRARY)
		message(FATAL_ERROR "LuaJIT shared library not found. Install it first: libluajit-5.1-2 (Debian/Ubuntu) or luajit (other distributions).")
	endif()
	message(STATUS "Using system LuaJIT: ${LUAJIT_LIBRARY}")
	add_library(lib_luajit INTERFACE)
	target_include_directories(lib_luajit INTERFACE ${PROJECT_SOURCE_DIR}/src/scripting/luajit)
	target_link_libraries(lib_luajit INTERFACE ${LUAJIT_LIBRARY})
endif()
