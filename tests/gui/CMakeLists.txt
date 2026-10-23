# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(gui_i18n_test i18n_test.c ../../sign/gui/i18n.c ../../sign/runtime.c)
target_include_directories(gui_i18n_test PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(gui_i18n_test PRIVATE sign_gui_deps)
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

add_executable(gui_navigation_test navigation_test.c ../../sign/gui/window.c ../../sign/gui/about.c
    ../../sign/gui/i18n.c ../../sign/runtime.c ../../sign/gui/settings.c
    ../../sign/preferences.c ../../sign/config.c ../../sign/arg.c ../../sign/options.c
    ../../sign/json.c ../../sign/io.c ../../sign/certs/certs.c "${SIGN_CERT_CATALOG}")
target_compile_definitions(gui_navigation_test PRIVATE SIGN_VERSION="${PROJECT_VERSION}")
target_include_directories(gui_navigation_test PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(gui_navigation_test PRIVATE sign_gui_deps sign_config_deps
    Argp::Argp libssig::ssig OpenSSL::Crypto PkgConfig::GIO)
target_compile_options(gui_navigation_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
# Native builds compile the test; Linux can exercise real allocations in Xvfb.
if(UNIX AND NOT APPLE)
    find_program(XVFB_RUN_EXECUTABLE xvfb-run)
    find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session)
    if(XVFB_RUN_EXECUTABLE AND DBUS_RUN_SESSION_EXECUTABLE)
        add_test(NAME gui_navigation COMMAND "${CMAKE_COMMAND}" -E env
            LC_ALL=en_US.UTF-8 LANGUAGE=es GDK_BACKEND=x11 GSK_RENDERER=cairo GTK_A11Y=none
            ADW_DISABLE_PORTAL=1 GIO_USE_VFS=local GSETTINGS_BACKEND=memory
            "${DBUS_RUN_SESSION_EXECUTABLE}" --config-file "${CMAKE_CURRENT_SOURCE_DIR}/session.conf"
            -- "${XVFB_RUN_EXECUTABLE}" -a "--server-args=-screen 0 1280x1024x24"
            $<TARGET_FILE:gui_navigation_test> "${CMAKE_CURRENT_BINARY_DIR}/snapshots")
        set_tests_properties(gui_navigation PROPERTIES TIMEOUT 30)
    endif()
endif()
