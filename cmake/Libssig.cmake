# SPDX-License-Identifier: GPL-3.0-or-later
# An explicit package path is useful when developing the sibling library.
# Otherwise build the pinned source with CRSign, without a separate install.
if(libssig_DIR)
    find_package(libssig 0.1.5 CONFIG REQUIRED)
else()
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/tests/libssig/CMakeLists.txt")
        message(FATAL_ERROR
            "Missing pinned libssig source. Run git submodule update --init --recursive "
            "or set libssig_DIR to an existing static libssig package.")
    endif()
    # A bundled libmagic archive should not require a runtime magic.mgc file.
    # Keep the cache truthful for the native packaging checks as well.
    set(LIBSSIG_EMBED_MAGIC_DATABASE ON CACHE BOOL "Embed libmagic's matching database" FORCE)
    # Keep dependency options local; CRSign owns testing and installation.
    function(sign_add_bundled_libssig)
        set(BUILD_SHARED_LIBS OFF)
        set(LIBSSIG_BUILD_TESTING OFF)
        set(LIBSSIG_INSTALL OFF)
        add_subdirectory("${PROJECT_SOURCE_DIR}/tests/libssig"
            "${PROJECT_BINARY_DIR}/libssig")
    endfunction()
    sign_add_bundled_libssig()
    message(STATUS "Building pinned libssig statically with CRSign")
endif()

get_target_property(SIGN_LIBSSIG_TYPE libssig::ssig TYPE)
if(NOT SIGN_LIBSSIG_TYPE STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "Build libssig with -DBUILD_SHARED_LIBS=OFF and select it with libssig_DIR")
endif()
