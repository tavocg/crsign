# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.16)

# These tests can run on a target OS before the rest of the CLI is ported.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(crsign_io_tests LANGUAGES C)
    enable_testing()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
endif()

add_executable(io_test io_test.c ../../sign/io.c)
target_include_directories(io_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(io_test PRIVATE PkgConfig::GIO)
set_target_properties(io_test PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(io_test PRIVATE /W4 /WX)
else()
    target_compile_options(io_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME file_io COMMAND io_test)
set_tests_properties(file_io PROPERTIES TIMEOUT 30)
