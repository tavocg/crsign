# SPDX-License-Identifier: GPL-3.0-or-later

find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_program(OPENSSL_EXECUTABLE openssl REQUIRED)

add_executable(timestamp_driver
    timestamp_driver.c
    ../sign/timestamp.c
    ../sign/http.c
    ../sign/trust.c
    ../sign/io.c
    ../sign/certs/certs.c
    "${SIGN_CERT_CATALOG}"
)
target_include_directories(timestamp_driver PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(timestamp_driver PRIVATE libssig::ssig CURL::libcurl OpenSSL::Crypto)
target_compile_options(timestamp_driver PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME trust_cli COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/trust_cli.py
    $<TARGET_FILE:sign> $<TARGET_FILE:timestamp_driver> ${OPENSSL_EXECUTABLE}
    $<TARGET_FILE:sign_mock>)
set_tests_properties(trust_cli PROPERTIES TIMEOUT 90)

# Exercise the real CLI without a card, PIN or installed PKCS#11 module.
add_executable(sign_mock ${SIGN_SOURCES} token_mock.c)
target_include_directories(sign_mock PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(sign_mock PRIVATE libssig::ssig CURL::libcurl OpenSSL::Crypto)
target_compile_definitions(sign_mock PRIVATE SIGN_VERSION="${PROJECT_VERSION}")
target_compile_options(sign_mock PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(NOT HEADLESS)
    target_link_libraries(sign_mock PRIVATE PkgConfig::GTK4)
    target_compile_definitions(sign_mock PRIVATE SIGN_WITH_GUI)
endif()
add_test(NAME commands_cli COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/commands_cli.py $<TARGET_FILE:sign_mock> ${OPENSSL_EXECUTABLE})
set_tests_properties(commands_cli PROPERTIES TIMEOUT 30)

# Reuse libssig's pinned suite, including its nested ETSI schema submodules.
option(SIGN_TEST_LIBSSIG "Run libssig conformance and real Sign fixture tests" ON)
if(SIGN_TEST_LIBSSIG)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libssig/tests/CMakeLists.txt")
        message(FATAL_ERROR
            "Missing tests/libssig. Run git submodule update --init --recursive "
            "or explicitly set SIGN_TEST_LIBSSIG=OFF for CLI-only tests.")
    endif()
    add_subdirectory(libssig/tests libssig)
    add_test(NAME signatures_cli COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/signatures_cli.py" $<TARGET_FILE:sign>
        "${CMAKE_CURRENT_BINARY_DIR}/libssig/ades-fixtures"
        "${CMAKE_CURRENT_BINARY_DIR}/libssig/document-fixtures")
    set_tests_properties(signatures_cli PROPERTIES
        FIXTURES_REQUIRED "libssig_ades;libssig_documents"
        LABELS "integration;conformance" TIMEOUT 120)
endif()

# Synthetic embedded bundles exercise selection without trusting test roots in Sign.
set(BUNDLE_FIXTURES "${CMAKE_CURRENT_BINARY_DIR}/bundle-fixtures")
set(BUNDLE_CATALOG "${CMAKE_CURRENT_BINARY_DIR}/bundle_catalog.c")
add_custom_command(OUTPUT "${BUNDLE_CATALOG}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/bundles_fixture.py"
        "${OPENSSL_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/sign/certs/embed.py"
        "${BUNDLE_CATALOG}" "${BUNDLE_FIXTURES}"
    DEPENDS bundles_fixture.py "${PROJECT_SOURCE_DIR}/sign/certs/embed.py"
    VERBATIM)
set(BUNDLE_SOURCES ${SIGN_SOURCES})
list(REMOVE_ITEM BUNDLE_SOURCES "${SIGN_CERT_CATALOG}")
add_library(bundle_catalog OBJECT "${BUNDLE_CATALOG}")
target_include_directories(bundle_catalog PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(bundle_catalog PRIVATE libssig::ssig)
add_executable(sign_bundles ${BUNDLE_SOURCES} $<TARGET_OBJECTS:bundle_catalog>)
target_include_directories(sign_bundles PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(sign_bundles PRIVATE libssig::ssig CURL::libcurl OpenSSL::Crypto)
target_compile_definitions(sign_bundles PRIVATE SIGN_VERSION="${PROJECT_VERSION}")
target_compile_options(sign_bundles PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(NOT HEADLESS)
    target_link_libraries(sign_bundles PRIVATE PkgConfig::GTK4)
    target_compile_definitions(sign_bundles PRIVATE SIGN_WITH_GUI)
endif()
add_executable(timestamp_bundles timestamp_driver.c ../sign/timestamp.c
    ../sign/http.c ../sign/trust.c ../sign/io.c ../sign/certs/certs.c $<TARGET_OBJECTS:bundle_catalog>)
target_include_directories(timestamp_bundles PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(timestamp_bundles PRIVATE libssig::ssig CURL::libcurl OpenSSL::Crypto)
target_compile_options(timestamp_bundles PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME bundles_cli COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/bundles_cli.py" $<TARGET_FILE:sign_bundles>
    $<TARGET_FILE:timestamp_bundles> "${OPENSSL_EXECUTABLE}" "${BUNDLE_FIXTURES}")
set_tests_properties(bundles_cli PROPERTIES TIMEOUT 90 LABELS "integration")

add_test(NAME cert_catalog COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/catalog_test.py" "${PROJECT_SOURCE_DIR}"
    "${OPENSSL_EXECUTABLE}")
set_tests_properties(cert_catalog PROPERTIES LABELS "integration" TIMEOUT 30)
