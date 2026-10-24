# SPDX-License-Identifier: GPL-3.0-or-later

# Each suite isolates signatures from the user's real document history.
function(sign_test_data_directory)
    get_property(local_tests DIRECTORY PROPERTY TESTS)
    foreach(test IN LISTS local_tests)
        set_property(TEST "${test}" APPEND PROPERTY
            ENVIRONMENT "XDG_DATA_HOME=${PROJECT_BINARY_DIR}/tests/data")
    endforeach()
endfunction()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_program(OPENSSL_EXECUTABLE openssl REQUIRED)

add_executable(timestamp_driver
    timestamp_driver.c
    ../sign/timestamp/timestamp.c
    ../sign/http.c
    ../sign/trust.c
    ../sign/io.c
    ../sign/certs/certs.c
    "${SIGN_CERT_CATALOG}"
)
target_include_directories(timestamp_driver PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(timestamp_driver PRIVATE libssig::ssig CURL::libcurl OpenSSL::Crypto PkgConfig::GIO)
target_compile_options(timestamp_driver PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME trust_cli COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/trust_cli.py
    $<TARGET_FILE:sign> $<TARGET_FILE:timestamp_driver> ${OPENSSL_EXECUTABLE}
    $<TARGET_FILE:sign_mock>)
set_tests_properties(trust_cli PROPERTIES TIMEOUT 90)

# Exercise the real CLI without a card, PIN or installed PKCS#11 module.
add_executable(sign_mock ${SIGN_SOURCES} token_mock.c)
target_include_directories(sign_mock PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(sign_mock PRIVATE Argp::Argp libssig::ssig CURL::libcurl OpenSSL::Crypto sign_config_deps PkgConfig::GIO sign_image_deps PkgConfig::JANSSON PkgConfig::P11_KIT)
target_compile_definitions(sign_mock PRIVATE SIGN_VERSION="${PROJECT_VERSION}")
target_compile_options(sign_mock PRIVATE "$<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Wpedantic;-Werror>")
if(NOT HEADLESS)
    target_link_libraries(sign_mock PRIVATE sign_gui_deps)
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
target_link_libraries(bundle_catalog PRIVATE libssig::ssig PkgConfig::GLIB)
add_executable(sign_bundles ${BUNDLE_SOURCES} $<TARGET_OBJECTS:bundle_catalog>)
target_include_directories(sign_bundles PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(sign_bundles PRIVATE Argp::Argp libssig::ssig CURL::libcurl OpenSSL::Crypto sign_config_deps PkgConfig::GIO sign_image_deps PkgConfig::JANSSON PkgConfig::P11_KIT)
target_compile_definitions(sign_bundles PRIVATE SIGN_VERSION="${PROJECT_VERSION}")
target_compile_options(sign_bundles PRIVATE "$<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Wpedantic;-Werror>")
if(NOT HEADLESS)
    target_link_libraries(sign_bundles PRIVATE sign_gui_deps)
endif()
add_executable(timestamp_bundles timestamp_driver.c ../sign/timestamp/timestamp.c
    ../sign/http.c ../sign/trust.c ../sign/io.c ../sign/certs/certs.c $<TARGET_OBJECTS:bundle_catalog>)
target_include_directories(timestamp_bundles PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(timestamp_bundles PRIVATE libssig::ssig CURL::libcurl OpenSSL::Crypto PkgConfig::GIO)
target_compile_options(timestamp_bundles PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME bundles_cli COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/bundles_cli.py" $<TARGET_FILE:sign_bundles>
    $<TARGET_FILE:timestamp_bundles> "${OPENSSL_EXECUTABLE}" "${BUNDLE_FIXTURES}")
set_tests_properties(bundles_cli PROPERTIES TIMEOUT 90 LABELS "integration")

add_test(NAME cert_catalog COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/catalog_test.py" "${PROJECT_SOURCE_DIR}"
    "${OPENSSL_EXECUTABLE}")
set_tests_properties(cert_catalog PROPERTIES LABELS "integration" TIMEOUT 30)

add_executable(authority_driver authority_driver.c ../sign/timestamp/authority.c
    ../sign/trust.c ../sign/io.c ../sign/certs/certs.c $<TARGET_OBJECTS:bundle_catalog>)
target_include_directories(authority_driver PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(authority_driver PRIVATE libssig::ssig OpenSSL::Crypto PkgConfig::GIO)
target_compile_options(authority_driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME tsa_authority COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/authority_test.py" $<TARGET_FILE:authority_driver>
    "${BUNDLE_FIXTURES}" "${OPENSSL_EXECUTABLE}")
set_tests_properties(tsa_authority PROPERTIES TIMEOUT 30 LABELS "integration")

add_executable(http_driver http_driver.c ../sign/http.c)
target_include_directories(http_driver PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(http_driver PRIVATE libssig::ssig CURL::libcurl)
target_compile_options(http_driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME http_transport COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/http_test.py" $<TARGET_FILE:http_driver>)
set_tests_properties(http_transport PROPERTIES TIMEOUT 30 LABELS "integration")

add_executable(args_driver args_driver.c ../sign/arg.c ../sign/options.c ../sign/config.c
    ../sign/json.c ../sign/io.c ../sign/certs/certs.c "${SIGN_CERT_CATALOG}")
target_include_directories(args_driver PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(args_driver PRIVATE Argp::Argp libssig::ssig OpenSSL::Crypto sign_config_deps PkgConfig::GIO)
target_compile_definitions(args_driver PRIVATE SIGN_VERSION="${PROJECT_VERSION}")
target_compile_options(args_driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME profiles_cli COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/profiles_cli.py" $<TARGET_FILE:args_driver>
    $<TARGET_FILE:sign_mock>)
set_tests_properties(profiles_cli PROPERTIES TIMEOUT 30)

add_subdirectory(io)

add_test(NAME arguments_cli COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/arguments_cli.py" $<TARGET_FILE:args_driver>)
set_tests_properties(arguments_cli PROPERTIES TIMEOUT 30)

add_subdirectory(terminal)
if(UNIX)
    add_test(NAME pin_signing COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/terminal/terminal_test.py"
        $<TARGET_FILE:terminal_driver> $<TARGET_FILE:sign_mock>)
    set_tests_properties(pin_signing PROPERTIES TIMEOUT 30)
endif()

add_test(NAME pincmd_cli COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/pincmd_cli.py" $<TARGET_FILE:sign_mock>
    $<TARGET_FILE:args_driver>)
set_tests_properties(pincmd_cli PROPERTIES TIMEOUT 30)

# Real dynamic loading through p11-kit, using isolated empty hardware fixtures.
pkg_check_modules(P11_KIT REQUIRED IMPORTED_TARGET p11-kit-1)
foreach(module IN ITEMS first second broken)
    add_library(pkcs11_${module} MODULE pkcs11_module.c)
    target_link_libraries(pkcs11_${module} PRIVATE PkgConfig::P11_KIT)
    target_compile_definitions(pkcs11_${module} PRIVATE MODULE_LABEL="Sign-${module}")
    target_compile_options(pkcs11_${module} PRIVATE -Wall -Wextra -Wpedantic -Werror)
endforeach()
target_compile_definitions(pkcs11_broken PRIVATE MODULE_FAIL)
add_test(NAME modules_cli COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules_cli.py" $<TARGET_FILE:sign>
    $<TARGET_FILE:args_driver> $<TARGET_FILE:sign_mock>
    $<TARGET_FILE:pkcs11_first> $<TARGET_FILE:pkcs11_second> $<TARGET_FILE:pkcs11_broken>)
set_tests_properties(modules_cli PROPERTIES TIMEOUT 30)

add_test(NAME native_packaging COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging_test.py")
set_tests_properties(native_packaging PROPERTIES TIMEOUT 30)

add_executable(runtime_driver runtime/driver.c ../sign/runtime.c)
target_include_directories(runtime_driver PRIVATE "${PROJECT_SOURCE_DIR}")
target_compile_definitions(runtime_driver PRIVATE SIGN_WITH_GUI)
target_link_libraries(runtime_driver PRIVATE PkgConfig::GIO)
target_compile_options(runtime_driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME portable_runtime COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/runtime_test.py"
    $<TARGET_FILE:runtime_driver>)
set_tests_properties(portable_runtime PROPERTIES TIMEOUT 30)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libssig/tests/document_sign_fixture.c")
    # Both the renderer and the library produce a cryptographically valid PDF here.
    add_executable(appearance_driver appearance_driver.c ../sign/appearance.c
        ../sign/arg.c ../sign/options.c ../sign/config.c ../sign/io.c ../sign/certs/certs.c
        "${SIGN_CERT_CATALOG}" libssig/tests/document_sign_fixture.c)
    target_include_directories(appearance_driver PRIVATE "${PROJECT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/libssig")
    target_link_libraries(appearance_driver PRIVATE Argp::Argp libssig::ssig OpenSSL::Crypto
        sign_config_deps PkgConfig::GIO sign_image_deps PkgConfig::P11_KIT)
    target_compile_definitions(appearance_driver PRIVATE SIGN_VERSION="${PROJECT_VERSION}")
    target_compile_options(appearance_driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME appearance_cli COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/appearance_cli.py" $<TARGET_FILE:appearance_driver>
        $<TARGET_FILE:args_driver>)
    set_tests_properties(appearance_cli PROPERTIES TIMEOUT 90)
endif()

if(NOT HEADLESS)
    add_subdirectory(gui)
endif()

add_subdirectory(flows)

add_executable(preferences_test preferences_test.c ../sign/preferences.c ../sign/config.c
    ../sign/arg.c ../sign/options.c ../sign/json.c ../sign/io.c ../sign/certs/certs.c "${SIGN_CERT_CATALOG}")
target_include_directories(preferences_test PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(preferences_test PRIVATE Argp::Argp libssig::ssig OpenSSL::Crypto sign_config_deps PkgConfig::GIO)
target_compile_definitions(preferences_test PRIVATE SIGN_VERSION="${PROJECT_VERSION}")
target_compile_options(preferences_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME preferences COMMAND preferences_test)


add_executable(history_driver history_driver.c ../sign/history.c ../sign/user_data.c ../sign/ui_preferences.c)
target_include_directories(history_driver PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(history_driver PRIVATE PkgConfig::GIO PkgConfig::JANSSON)
target_compile_options(history_driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME document_history COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/history_test.py" $<TARGET_FILE:history_driver>)
set_tests_properties(document_history PROPERTIES TIMEOUT 30)

sign_test_data_directory()
