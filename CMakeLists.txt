# This file is part of CRSign.
# Copyright (C) 2026 Gustavo Calvo <tavo@tavo.cr>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# See sign/main.c for the full copyright and warranty notice.
# See LICENSE in the project root for the GNU General Public License.

cmake_minimum_required(VERSION 3.16)

project(CRSign VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(Argp REQUIRED)

include(GNUInstallDirs)
if(APPLE)
    set(CMAKE_INSTALL_RPATH "@executable_path/../lib")
elseif(UNIX)
    set(CMAKE_INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()
option(BUILD_TESTING "Build the test suites" OFF)
include(CTest)
find_package(CURL 7.85 REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
include_directories(BEFORE SYSTEM "${OPENSSL_INCLUDE_DIR}")
find_program(OPENSSL_EXECUTABLE openssl REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0>=2.40)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
pkg_check_modules(JANSSON REQUIRED IMPORTED_TARGET jansson>=2.11)
pkg_check_modules(P11_KIT REQUIRED IMPORTED_TARGET p11-kit-1)
pkg_check_modules(IMAGES REQUIRED IMPORTED_TARGET librsvg-2.0>=2.52 gdk-pixbuf-2.0 cairo pangocairo)
add_library(sign_image_deps INTERFACE)
target_link_libraries(sign_image_deps INTERFACE PkgConfig::IMAGES)
if(UNIX)
    target_link_libraries(sign_image_deps INTERFACE m)
endif()
find_path(TOMLC17_INCLUDE_DIR tomlc17.h REQUIRED)
find_library(TOMLC17_LIBRARY NAMES tomlc17 REQUIRED)
include(Libssig)

add_library(sign_config_deps INTERFACE)
target_include_directories(sign_config_deps INTERFACE "${TOMLC17_INCLUDE_DIR}")
target_link_libraries(sign_config_deps INTERFACE PkgConfig::GLIB "${TOMLC17_LIBRARY}")

option(HEADLESS "Build without the GTK graphical interface" OFF)

if(NOT HEADLESS)
    pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4>=4.10)
    pkg_check_modules(ADWAITA REQUIRED IMPORTED_TARGET libadwaita-1>=1.4)
    pkg_check_modules(PDF_VIEW REQUIRED IMPORTED_TARGET poppler-glib>=0.82)
    # libqpdf.pc exports C++ flags; the preview consumes only its C API.
    find_path(SIGN_QPDF_INCLUDE_DIR qpdf/qpdf-c.h REQUIRED)
    find_library(SIGN_QPDF_LIBRARY NAMES qpdf REQUIRED)
    find_package(Intl REQUIRED)
    add_subdirectory(po)
    configure_file(sign/gui/config.h.in generated/sign/gui/config.h @ONLY)
    add_library(sign_gui_deps INTERFACE)
    target_link_libraries(sign_gui_deps INTERFACE PkgConfig::GTK4 PkgConfig::ADWAITA PkgConfig::PDF_VIEW ${SIGN_QPDF_LIBRARY} ${Intl_LIBRARIES})
    target_include_directories(sign_gui_deps SYSTEM INTERFACE "${SIGN_QPDF_INCLUDE_DIR}" ${Intl_INCLUDE_DIRS})
    target_include_directories(sign_gui_deps INTERFACE "${PROJECT_BINARY_DIR}/generated")
    target_compile_definitions(sign_gui_deps INTERFACE SIGN_WITH_GUI GETTEXT_PACKAGE="crsign")
    find_program(GLIB_COMPILE_RESOURCES_EXECUTABLE glib-compile-resources REQUIRED)
    set(SIGN_GUI_RESOURCES "${PROJECT_BINARY_DIR}/generated/sign/gui/resources.c")
    add_custom_command(OUTPUT "${SIGN_GUI_RESOURCES}"
        COMMAND "${GLIB_COMPILE_RESOURCES_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/sign/gui/resources.gresource.xml"
            --sourcedir "${PROJECT_SOURCE_DIR}" --generate-source --c-name sign_gui
            --target "${SIGN_GUI_RESOURCES}"
        DEPENDS sign/gui/resources.gresource.xml packaging/icon-rounded.svg sign/gui/style.css
        VERBATIM)
    add_library(sign_gui_resources OBJECT "${SIGN_GUI_RESOURCES}")
    target_link_libraries(sign_gui_resources PRIVATE PkgConfig::GIO)
    target_sources(sign_gui_deps INTERFACE $<TARGET_OBJECTS:sign_gui_resources>)
    add_dependencies(sign_gui_deps sign_gui_resources)
    add_dependencies(sign_gui_deps translations)
endif()

file(GLOB_RECURSE SIGN_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/sign/*.c"
)
list(FILTER SIGN_SOURCES EXCLUDE REGEX "_test\\.c$")

if(HEADLESS)
    list(FILTER SIGN_SOURCES EXCLUDE REGEX "/sign/gui(\\.c$|/)")
endif()

# The catalogue is local and deterministic; configure never downloads certificates.
file(GLOB_RECURSE SIGN_CERT_MANIFESTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/sign/certs/*/manifest.json")
file(GLOB_RECURSE SIGN_CERT_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/sign/certs/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${SIGN_CERT_MANIFESTS} ${SIGN_CERT_FILES} "${CMAKE_CURRENT_SOURCE_DIR}/sign/certs/embed.py")
set(SIGN_CERT_CATALOG "${CMAKE_CURRENT_BINARY_DIR}/cert_catalog.c")
execute_process(COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/sign/certs/embed.py"
    --openssl "${OPENSSL_EXECUTABLE}"
    "${SIGN_CERT_CATALOG}" ${SIGN_CERT_MANIFESTS}
    RESULT_VARIABLE SIGN_CERT_RESULT)
if(NOT SIGN_CERT_RESULT EQUAL 0)
    message(FATAL_ERROR "Could not generate the embedded certificate catalogue")
endif()
list(APPEND SIGN_SOURCES "${SIGN_CERT_CATALOG}")

if(WIN32)
    enable_language(RC)
    configure_file(packaging/windows.rc.in "${CMAKE_CURRENT_BINARY_DIR}/windows.rc" @ONLY)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/windows.rc" PROPERTIES
        OBJECT_DEPENDS "${PROJECT_SOURCE_DIR}/packaging/windows.manifest")
    list(APPEND SIGN_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/windows.rc")
endif()

add_executable(sign ${SIGN_SOURCES})

target_link_libraries(sign PRIVATE Argp::Argp libssig::ssig CURL::libcurl OpenSSL::Crypto sign_config_deps PkgConfig::GIO sign_image_deps PkgConfig::JANSSON PkgConfig::P11_KIT)

target_include_directories(sign PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(NOT HEADLESS)
    target_link_libraries(sign PRIVATE sign_gui_deps)
endif()

target_compile_definitions(sign PRIVATE
    SIGN_VERSION="${PROJECT_VERSION}"
)

target_compile_options(sign PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Wpedantic;-Werror>"
)

install(TARGETS sign
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
