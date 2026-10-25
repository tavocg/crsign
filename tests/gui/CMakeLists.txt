# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(gui_i18n_test i18n_test.c ../../sign/appearance.c ../../sign/io.c ../../sign/gui/error.c ../../sign/gui/i18n.c ../../sign/runtime.c)
target_include_directories(gui_i18n_test PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(gui_i18n_test PRIVATE sign_gui_deps sign_image_deps libssig::ssig OpenSSL::Crypto PkgConfig::P11_KIT)
target_compile_options(gui_i18n_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
foreach(language IN ITEMS en es)
    add_test(NAME gui_i18n_${language} COMMAND "${CMAKE_COMMAND}" -E env
        LC_ALL=en_US.UTF-8 LANGUAGE=${language} $<TARGET_FILE:gui_i18n_test> ${language})
endforeach()

if(NOT IS_ABSOLUTE "${CMAKE_INSTALL_LOCALEDIR}")
    add_test(NAME gui_i18n_installed COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/installed_i18n.py" $<TARGET_FILE:gui_i18n_test>
        "${PROJECT_BINARY_DIR}/locale" "${CMAKE_INSTALL_LOCALEDIR}")
    set_tests_properties(gui_i18n_installed PROPERTIES
        ENVIRONMENT "LC_ALL=en_US.UTF-8;LANGUAGE=es")
endif()

add_executable(gui_device_access_test device_access_test.c ../../sign/gui/device_access.c)
target_include_directories(gui_device_access_test PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(gui_device_access_test PRIVATE PkgConfig::GLIB)
target_compile_options(gui_device_access_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME gui_device_access COMMAND $<TARGET_FILE:gui_device_access_test>)
set_tests_properties(gui_device_access PROPERTIES TIMEOUT 10)

set(GUI_TEST_SOURCES
    ../../sign/gui/home.c ../../sign/gui/file_actions.c ../../sign/gui/window.c ../../sign/gui/about.c ../../sign/gui/validation.c
    ../../sign/gui/devices.c ../../sign/gui/inventory.c ../../sign/gui/certificate.c
    ../../sign/gui/device_access.c
    ../../sign/gui/file_drop.c ../../sign/gui/error.c ../../sign/gui/pdf_reader.c ../../sign/gui/pdf_view.c ../../sign/gui/signing.c
    ../../sign/flow.c ../../sign/user_data.c ../../sign/ui_preferences.c ../../sign/history.c ../../sign/execution.c ../../sign/sign.c ../../sign/modules.c
    ../../sign/appearance.c ../../sign/pin.c ../../sign/terminal.c ../../sign/http.c
    ../../sign/timestamp/authority.c ../../sign/trust.c
    ../../sign/gui/i18n.c ../../sign/runtime.c ../../sign/gui/settings.c
    ../../sign/preferences.c ../../sign/config.c ../../sign/arg.c ../../sign/options.c
    ../../sign/json.c ../../sign/io.c ../../sign/certs/certs.c "${SIGN_CERT_CATALOG}")
add_executable(gui_navigation_test navigation_test.c ${GUI_TEST_SOURCES})
add_executable(gui_validation_test validation_test.c ${GUI_TEST_SOURCES})
add_executable(gui_home_test home_test.c ${GUI_TEST_SOURCES})
add_executable(gui_drop_test drop_test.c ${GUI_TEST_SOURCES})
set(GUI_DEVICE_TEST_SOURCES ${GUI_TEST_SOURCES})
list(REMOVE_ITEM GUI_DEVICE_TEST_SOURCES "${SIGN_CERT_CATALOG}")
add_executable(gui_devices_test devices_test.c ${GUI_DEVICE_TEST_SOURCES} $<TARGET_OBJECTS:bundle_catalog>)
set(GUI_TEST_TARGETS gui_home_test gui_navigation_test gui_validation_test gui_drop_test gui_devices_test)
if(EXISTS "${PROJECT_SOURCE_DIR}/vendor/libssig/tests/document_sign_fixture.c")
    add_executable(gui_signing_test signing_test.c ../flows/crypto_module.c ${GUI_TEST_SOURCES})
    sign_test_token_api(gui_signing_test)
    # The PKCS#11 fixture includes private headers that installed libssig omits.
    target_include_directories(gui_signing_test PRIVATE "${PROJECT_SOURCE_DIR}/vendor/libssig")
    list(APPEND GUI_TEST_TARGETS gui_signing_test)
endif()
foreach(target IN LISTS GUI_TEST_TARGETS)
    target_compile_definitions(${target} PRIVATE SIGN_VERSION="${PROJECT_VERSION}")
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(${target} PRIVATE sign_gui_deps sign_config_deps
        Argp::Argp libssig::ssig OpenSSL::Crypto PkgConfig::GIO
        CURL::libcurl sign_image_deps PkgConfig::JANSSON PkgConfig::P11_KIT sign_opensc_deps)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
endforeach()
# Native builds compile the test; Linux can exercise real allocations in Xvfb.
if(UNIX AND NOT APPLE)
    find_program(XVFB_RUN_EXECUTABLE xvfb-run)
    find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session)
    if(XVFB_RUN_EXECUTABLE AND DBUS_RUN_SESSION_EXECUTABLE)
        set(GUI_TEST_COMMAND "${CMAKE_COMMAND}" -E env
            LC_ALL=en_US.UTF-8 LANGUAGE=es GDK_BACKEND=x11 GSK_RENDERER=cairo GTK_A11Y=none
            ADW_DISABLE_PORTAL=1 GIO_USE_VFS=local GSETTINGS_BACKEND=memory G_DEBUG=fatal-criticals
            "${DBUS_RUN_SESSION_EXECUTABLE}" --config-file "${CMAKE_CURRENT_SOURCE_DIR}/session.conf"
            -- "${XVFB_RUN_EXECUTABLE}" -a "--server-args=-screen 0 1280x1024x24 -noreset")
        add_test(NAME gui_navigation COMMAND ${GUI_TEST_COMMAND}
            $<TARGET_FILE:gui_navigation_test> "${CMAKE_CURRENT_BINARY_DIR}/snapshots")
        if(EXISTS "${PROJECT_SOURCE_DIR}/vendor/libssig/tests/document_sign_fixture.c")
            add_test(NAME gui_validation COMMAND ${GUI_TEST_COMMAND}
                "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/validation_test.py"
                $<TARGET_FILE:gui_validation_test> $<TARGET_FILE:flow_crypto_driver>
                "${CMAKE_CURRENT_BINARY_DIR}/snapshots")
            set_tests_properties(gui_validation PROPERTIES TIMEOUT 45 RESOURCE_LOCK gtk_display)
            add_test(NAME gui_signing COMMAND ${GUI_TEST_COMMAND}
                "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/signing_test.py"
                $<TARGET_FILE:gui_signing_test> $<TARGET_FILE:sign>
                "${CMAKE_CURRENT_BINARY_DIR}/snapshots")
            set_tests_properties(gui_signing PROPERTIES TIMEOUT 75 RESOURCE_LOCK gtk_display)
        endif()
        set_tests_properties(gui_navigation PROPERTIES TIMEOUT 30 RESOURCE_LOCK gtk_display)
        add_test(NAME gui_home COMMAND ${GUI_TEST_COMMAND}
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/home_test.py"
            $<TARGET_FILE:gui_home_test> "${CMAKE_CURRENT_BINARY_DIR}/snapshots")
        set_tests_properties(gui_home PROPERTIES TIMEOUT 90 RESOURCE_LOCK gtk_display)

        if(TARGET device_fixture)
            add_test(NAME gui_devices COMMAND ${GUI_TEST_COMMAND}
                "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/devices_test.py"
                $<TARGET_FILE:gui_devices_test> $<TARGET_FILE:device_fixture>
                "${BUNDLE_FIXTURES}" "${CMAKE_CURRENT_BINARY_DIR}/snapshots" "${OPENSSL_EXECUTABLE}")
            set_tests_properties(gui_devices PROPERTIES TIMEOUT 60 RESOURCE_LOCK gtk_display)
        endif()
        find_program(XDOTOOL_EXECUTABLE xdotool)
        if(XDOTOOL_EXECUTABLE)
            add_test(NAME gui_native_drop COMMAND ${GUI_TEST_COMMAND}
                "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/drop_test.py"
                $<TARGET_FILE:gui_drop_test> "${XDOTOOL_EXECUTABLE}")
            set_tests_properties(gui_native_drop PROPERTIES TIMEOUT 75 RESOURCE_LOCK gtk_display)
        endif()
    endif()
endif()

sign_test_data_directory()
