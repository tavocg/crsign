# SPDX-License-Identifier: GPL-3.0-or-later
# Restrict only the small dependencies below, not GTK or system libraries.
function(sign_find_static_library variable)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a .lib)
    find_library(${variable} ${ARGN} REQUIRED)
    if(NOT "${${variable}}" MATCHES "\\.(a|lib)$")
        message(FATAL_ERROR "${variable} must name a static archive")
    endif()
    set(${variable} "${${variable}}" PARENT_SCOPE)
endfunction()

find_package(PkgConfig REQUIRED)
pkg_check_modules(JANSSON REQUIRED IMPORTED_TARGET jansson>=2.11)
sign_find_static_library(SIGN_JANSSON_LIBRARY NAMES jansson
    HINTS ${JANSSON_LIBRARY_DIRS})
# libssig exports this same target in its static link interface. Select the
# archive once for both consumers rather than linking two copies of Jansson.
set_property(TARGET PkgConfig::JANSSON PROPERTY INTERFACE_LINK_LIBRARIES
    "${SIGN_JANSSON_LIBRARY}")

pkg_check_modules(SIGN_ZLIB REQUIRED zlib)
sign_find_static_library(SIGN_ZLIB_LIBRARY NAMES z zlibstatic zlib
    HINTS ${SIGN_ZLIB_LIBRARY_DIRS})
set(ZLIB_LIBRARY "${SIGN_ZLIB_LIBRARY}")
set(ZLIB_LIBRARY_RELEASE "${SIGN_ZLIB_LIBRARY}")
set(ZLIB_LIBRARY_DEBUG "${SIGN_ZLIB_LIBRARY}")
find_package(ZLIB REQUIRED)

find_path(TOMLC17_INCLUDE_DIR tomlc17.h REQUIRED)
sign_find_static_library(TOMLC17_LIBRARY NAMES tomlc17)
