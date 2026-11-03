cmake_minimum_required(VERSION 3.15)

if(WIN32)
	message(STATUS "A certain luajit version is distributed with Alice")
	add_library(lib_luajit INTERFACE)
	target_link_libraries(lib_luajit INTERFACE ${PROJECT_SOURCE_DIR}/libs/luajit.lib)
else()
	# PATCHED: no network fetch; use system libluajit with vendored headers
	message(STATUS "Using system LuaJIT (local patch)")
	add_library(lib_luajit INTERFACE)
	target_include_directories(lib_luajit INTERFACE ${PROJECT_SOURCE_DIR}/src/scripting/luajit)
	target_link_libraries(lib_luajit INTERFACE /usr/lib/x86_64-linux-gnu/libluajit-5.1.so.2)
endif()
