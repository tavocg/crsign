# SPDX-License-Identifier: GPL-3.0-or-later
# Incorporate the signing engine dependencies, keeping GTK and system libraries dynamic.
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

# Keep the same libcrypto archive across CRSign, libssig and QPDF. Use fresh
# selectors so an existing build cache containing DLL/.so paths also migrates.
pkg_check_modules(SIGN_CRYPTO REQUIRED libcrypto>=3.0)
sign_find_static_library(SIGN_CRYPTO_STATIC_LIBRARY NAMES crypto libcrypto
    HINTS "${OPENSSL_ROOT_DIR}/lib" ${SIGN_CRYPTO_LIBRARY_DIRS})
sign_find_static_library(SIGN_SSL_STATIC_LIBRARY NAMES ssl libssl
    HINTS "${OPENSSL_ROOT_DIR}/lib" ${SIGN_CRYPTO_LIBRARY_DIRS})
find_path(SIGN_CRYPTO_INCLUDE_DIR openssl/opensslv.h
    HINTS "${OPENSSL_ROOT_DIR}/include" ${SIGN_CRYPTO_INCLUDE_DIRS} REQUIRED)
# FindOpenSSL is called again by libssig. Persist its inputs before its
# mark_as_advanced calls can create empty cache entries on a first configure.
set(OPENSSL_USE_STATIC_LIBS TRUE)
set(OPENSSL_CRYPTO_LIBRARY "${SIGN_CRYPTO_STATIC_LIBRARY}" CACHE FILEPATH "Static OpenSSL crypto archive" FORCE)
set(OPENSSL_SSL_LIBRARY "${SIGN_SSL_STATIC_LIBRARY}" CACHE FILEPATH "Static OpenSSL SSL archive" FORCE)
set(OPENSSL_INCLUDE_DIR "${SIGN_CRYPTO_INCLUDE_DIR}" CACHE PATH "Matching OpenSSL headers" FORCE)
if(MINGW)
    set(LIB_EAY "${SIGN_CRYPTO_STATIC_LIBRARY}" CACHE FILEPATH "Static MinGW crypto archive" FORCE)
    set(SSL_EAY "${SIGN_SSL_STATIC_LIBRARY}" CACHE FILEPATH "Static MinGW SSL archive" FORCE)
endif()
if(WIN32 OR APPLE)
    find_package(OpenSSL 3.5 REQUIRED COMPONENTS Crypto)
else()
    find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
endif()

pkg_check_modules(SIGN_JPEG REQUIRED libjpeg)
sign_find_static_library(SIGN_JPEG_STATIC_LIBRARY NAMES jpeg jpeg-static
    HINTS ${SIGN_JPEG_LIBRARY_DIRS})
set(JPEG_LIBRARY "${SIGN_JPEG_STATIC_LIBRARY}")
set(JPEG_LIBRARY_RELEASE "${SIGN_JPEG_STATIC_LIBRARY}")
set(JPEG_LIBRARY_DEBUG "${SIGN_JPEG_STATIC_LIBRARY}")
find_package(JPEG REQUIRED)

# Resolve pkg-config's private link dependencies, keeping zlib on the archive
# already selected above. Public pkg-config targets omit these for shared libs.
function(sign_static_pkg_dependencies output prefix own_library archive)
    set(libraries "${archive}")
    foreach(dependency IN LISTS ${prefix}_STATIC_LIBRARIES)
        if(dependency STREQUAL own_library)
            continue()
        elseif(dependency STREQUAL "z")
            list(APPEND libraries ZLIB::ZLIB)
        elseif(dependency STREQUAL "crypto")
            list(APPEND libraries OpenSSL::Crypto)
        elseif(dependency STREQUAL "jpeg")
            list(APPEND libraries JPEG::JPEG)
        else()
            find_library(${prefix}_DEPENDENCY_${dependency} NAMES "${dependency}"
                HINTS ${${prefix}_STATIC_LIBRARY_DIRS} REQUIRED)
            list(APPEND libraries "${${prefix}_DEPENDENCY_${dependency}}")
        endif()
    endforeach()
    set(${output} "${libraries}" PARENT_SCOPE)
endfunction()

# Include platform dependencies recorded by libcrypto.pc (for example bcrypt
# on Windows), in addition to those provided by CMake's OpenSSL finder.
sign_static_pkg_dependencies(SIGN_CRYPTO_PRIVATE_LIBRARIES SIGN_CRYPTO crypto
    "${SIGN_CRYPTO_STATIC_LIBRARY}")
list(REMOVE_ITEM SIGN_CRYPTO_PRIVATE_LIBRARIES "${SIGN_CRYPTO_STATIC_LIBRARY}")
set_property(TARGET OpenSSL::Crypto APPEND PROPERTY INTERFACE_LINK_LIBRARIES
    "${SIGN_CRYPTO_PRIVATE_LIBRARIES}")
set_property(TARGET OpenSSL::Crypto APPEND PROPERTY INTERFACE_LINK_OPTIONS
    "${SIGN_CRYPTO_STATIC_LDFLAGS_OTHER}")

pkg_check_modules(MAGIC REQUIRED IMPORTED_TARGET libmagic)
sign_find_static_library(SIGN_MAGIC_STATIC_LIBRARY NAMES magic
    HINTS ${MAGIC_LIBRARY_DIRS})
sign_static_pkg_dependencies(SIGN_MAGIC_LINK_LIBRARIES MAGIC magic "${SIGN_MAGIC_STATIC_LIBRARY}")
set_target_properties(PkgConfig::MAGIC PROPERTIES
    INTERFACE_LINK_LIBRARIES "${SIGN_MAGIC_LINK_LIBRARIES}"
    INTERFACE_LINK_OPTIONS "${MAGIC_STATIC_LDFLAGS_OTHER}")

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
