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

# Resolve pkg-config's private link dependencies, keeping zlib on the archive
# already selected above. Public pkg-config targets omit these for shared libs.
function(sign_static_pkg_dependencies output prefix own_library archive)
    set(libraries "${archive}")
    foreach(dependency IN LISTS ${prefix}_STATIC_LIBRARIES)
        if(dependency STREQUAL own_library)
            continue()
        elseif(dependency STREQUAL "z")
            list(APPEND libraries ZLIB::ZLIB)
        else()
            find_library(${prefix}_DEPENDENCY_${dependency} NAMES "${dependency}"
                HINTS ${${prefix}_STATIC_LIBRARY_DIRS} REQUIRED)
            list(APPEND libraries "${${prefix}_DEPENDENCY_${dependency}}")
        endif()
    endforeach()
    set(${output} "${libraries}" PARENT_SCOPE)
endfunction()

pkg_check_modules(XML2 REQUIRED IMPORTED_TARGET libxml-2.0)
sign_find_static_library(SIGN_XML2_LIBRARY NAMES xml2 libxml2
    HINTS ${XML2_LIBRARY_DIRS})
sign_static_pkg_dependencies(SIGN_XML2_LINK_LIBRARIES XML2 xml2 "${SIGN_XML2_LIBRARY}")
set_target_properties(PkgConfig::XML2 PROPERTIES
    INTERFACE_LINK_LIBRARIES "${SIGN_XML2_LINK_LIBRARIES}"
    INTERFACE_LINK_OPTIONS "${XML2_STATIC_LDFLAGS_OTHER}")
if(WIN32)
    target_compile_definitions(PkgConfig::XML2 INTERFACE LIBXML_STATIC)
endif()

# QPDF's .pc file exports C++ compiler flags; CRSign consumes only its C API.
# A C++ imported target supplies the runtime without applying those flags to C.
pkg_check_modules(SIGN_QPDF REQUIRED libqpdf>=11)
sign_find_static_library(SIGN_QPDF_STATIC_LIBRARY NAMES qpdf qpdf_static
    HINTS ${SIGN_QPDF_LIBRARY_DIRS})
sign_static_pkg_dependencies(SIGN_QPDF_LINK_LIBRARIES SIGN_QPDF qpdf "${SIGN_QPDF_STATIC_LIBRARY}")
list(REMOVE_ITEM SIGN_QPDF_LINK_LIBRARIES "${SIGN_QPDF_STATIC_LIBRARY}")
set(SIGN_QPDF_PRIVATE_LIBRARIES "${SIGN_QPDF_LINK_LIBRARIES}")
find_path(QPDF_INCLUDE_DIR qpdf/qpdf-c.h HINTS ${SIGN_QPDF_INCLUDE_DIRS} REQUIRED)
set(QPDF_LIBRARY "${SIGN_QPDF_STATIC_LIBRARY}")
add_library(LibssigQPDF::LibssigQPDF STATIC IMPORTED)
set_target_properties(LibssigQPDF::LibssigQPDF PROPERTIES
    IMPORTED_LOCATION "${SIGN_QPDF_STATIC_LIBRARY}"
    IMPORTED_LINK_INTERFACE_LANGUAGES CXX
    INTERFACE_INCLUDE_DIRECTORIES "${QPDF_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES "${SIGN_QPDF_PRIVATE_LIBRARIES}"
    INTERFACE_LINK_OPTIONS "${SIGN_QPDF_STATIC_LDFLAGS_OTHER}")
