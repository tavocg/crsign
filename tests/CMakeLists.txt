# SPDX-License-Identifier: GPL-3.0-or-later

find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_program(OPENSSL_EXECUTABLE openssl REQUIRED)

add_executable(timestamp_driver
    timestamp_driver.c
    ../sign/timestamp.c
    ../sign/trust.c
    ../sign/io.c
)
target_include_directories(timestamp_driver PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(timestamp_driver PRIVATE libssig::ssig CURL::libcurl)
target_compile_options(timestamp_driver PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME trust_cli COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/trust_cli.py
    $<TARGET_FILE:sign> $<TARGET_FILE:timestamp_driver> ${OPENSSL_EXECUTABLE})
set_tests_properties(trust_cli PROPERTIES TIMEOUT 90)
