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

include(GNUInstallDirs)
include(CTest)
find_package(libssig CONFIG REQUIRED)
find_package(CURL 7.85 REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_program(OPENSSL_EXECUTABLE openssl REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

option(HEADLESS "Build without the GTK graphical interface" OFF)

if(NOT HEADLESS)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
endif()

file(GLOB_RECURSE SIGN_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/sign/*.c"
)
list(FILTER SIGN_SOURCES EXCLUDE REGEX "_test\\.c$")

if(HEADLESS)
    list(FILTER SIGN_SOURCES EXCLUDE REGEX "/sign/gui\\.c$")
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

add_executable(sign ${SIGN_SOURCES})

target_link_libraries(sign PRIVATE libssig::ssig CURL::libcurl OpenSSL::Crypto)

target_include_directories(sign PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(NOT HEADLESS)
    target_link_libraries(sign PRIVATE
        PkgConfig::GTK4
    )

    target_compile_definitions(sign PRIVATE
        SIGN_WITH_GUI
    )
endif()

target_compile_definitions(sign PRIVATE
    SIGN_VERSION="${PROJECT_VERSION}"
)

target_compile_options(sign PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

install(TARGETS sign
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
