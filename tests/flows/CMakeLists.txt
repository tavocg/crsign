# SPDX-License-Identifier: GPL-3.0-or-later
set(FLOW_ENGINE_SOURCES
    ../../sign/flow.c ../../sign/options.c ../../sign/execution.c ../../sign/sign.c
    ../../sign/modules.c ../../sign/appearance.c ../../sign/trust.c
    ../../sign/io.c ../../sign/pin.c ../../sign/terminal.c ../../sign/http.c
    ../../sign/timestamp/authority.c ../../sign/certs/certs.c "${SIGN_CERT_CATALOG}")
add_executable(flow_driver driver.c ../token_mock.c ${FLOW_ENGINE_SOURCES})
target_include_directories(flow_driver PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(flow_driver PRIVATE libssig::ssig CURL::libcurl OpenSSL::Crypto
    sign_image_deps PkgConfig::GIO PkgConfig::JANSSON PkgConfig::P11_KIT)
target_compile_options(flow_driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME flows COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/flow_test.py"
    $<TARGET_FILE:flow_driver> "${PROJECT_SOURCE_DIR}/doc/flow.schema.json")
set_tests_properties(flows PROPERTIES TIMEOUT 45)

if(EXISTS "${PROJECT_SOURCE_DIR}/tests/libssig/tests/document_sign_fixture.c")
    add_executable(flow_crypto_driver driver.c crypto_module.c ${FLOW_ENGINE_SOURCES})
    target_include_directories(flow_crypto_driver PRIVATE "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/tests/libssig")
    target_link_libraries(flow_crypto_driver PRIVATE libssig::ssig CURL::libcurl OpenSSL::Crypto
        sign_image_deps PkgConfig::GIO PkgConfig::JANSSON PkgConfig::P11_KIT)
    target_compile_options(flow_crypto_driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME flow_signatures COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/crypto_test.py" $<TARGET_FILE:flow_crypto_driver> $<TARGET_FILE:sign>)
    set_tests_properties(flow_signatures PROPERTIES TIMEOUT 45)
endif()
