cmake_minimum_required(VERSION 3.16)
project(meth C)

# The same build as premake5.lua and the Makefile, for CMake:
#
#	cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release
#	cmake --build build/cmake
#	ctest --test-dir build/cmake --output-on-failure
#
# Outputs go to build/cmake. Sanitizers: -DML_SANITIZE=ON, which also uses the
# system allocator for the heap (leak checks, see CLAUDE.md).

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Debug)
endif()

option(ML_SANITIZE "Build with address and undefined behaviour sanitizers" OFF)

# All library code: everything under source/meth. OS specific code lives in
# platform/<os>/; Linux and macOS share platform/posix/.
file(GLOB_RECURSE ML_LIB_SOURCES CONFIGURE_DEPENDS source/meth/*.c)

# platform/wasm/ is for the WebAssembly build (build_web.bat) only.
list(FILTER ML_LIB_SOURCES EXCLUDE REGEX "/platform/wasm/")

if(WIN32)
	list(FILTER ML_LIB_SOURCES EXCLUDE REGEX "/platform/posix/")
else()
	list(FILTER ML_LIB_SOURCES EXCLUDE REGEX "/platform/win32/")
endif()

add_library(meth_lib STATIC ${ML_LIB_SOURCES})
target_include_directories(meth_lib PUBLIC include)

# A warning is an error (/WX, -Werror in the Makefile).
if(MSVC)
	target_compile_options(meth_lib PUBLIC /W4 /WX)
	target_compile_definitions(meth_lib PUBLIC _CRT_SECURE_NO_WARNINGS)
else()
	target_compile_options(meth_lib PUBLIC -Wall -Wextra -Wno-comment -Werror)
	target_link_libraries(meth_lib PUBLIC m) # the math functions
endif()

if(ML_SANITIZE AND NOT MSVC)
	target_compile_options(meth_lib PUBLIC
		-fsanitize=address,undefined -fno-sanitize-recover=undefined -g)
	target_link_options(meth_lib PUBLIC -fsanitize=address,undefined)
	target_compile_definitions(meth_lib PUBLIC ML_HEAP_SYSTEM)
endif()

# The interactive shell
add_executable(meth source/app/main.c)
target_link_libraries(meth PRIVATE meth_lib)

# Unit tests; each group is also a test of its own for ctest
file(GLOB_RECURSE ML_TEST_SOURCES CONFIGURE_DEPENDS source/tests/*.c)
add_executable(tests ${ML_TEST_SOURCES})
target_link_libraries(tests PRIVATE meth_lib)

# The tests read the sources and data, so they run in the source folder.
# One ctest entry per group, the names are read from source/tests/main.c.
enable_testing()
file(STRINGS source/tests/main.c ML_GROUP_LINES REGEX "\\.name = \"[a-z_0-9]+\"")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS source/tests/main.c)

foreach(line IN LISTS ML_GROUP_LINES)
	string(REGEX REPLACE ".*\\.name = \"([a-z_0-9]+)\".*" "\\1" group "${line}")
	add_test(NAME ${group} COMMAND tests ${group}
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endforeach()

# Prints decoded key events, for trying out the console layer by hand
add_executable(keydemo source/demo/keydemo.c)
target_link_libraries(keydemo PRIVATE meth_lib)
