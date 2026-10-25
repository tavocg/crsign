# SPDX-License-Identifier: GPL-3.0-or-later
option(SIGN_EMBED_OPENSC "Build the pinned OpenSC PKCS#11 provider into sign" ON)
if(NOT SIGN_EMBED_OPENSC)
    add_library(sign_opensc_deps INTERFACE)
    message(STATUS "Embedded OpenSC disabled; using external PKCS#11 modules")
    return()
endif()

set(SIGN_OPENSC_SOURCE "${PROJECT_SOURCE_DIR}/vendor/opensc")
if(NOT EXISTS "${SIGN_OPENSC_SOURCE}/configure.ac")
    message(FATAL_ERROR
        "Missing vendor/opensc. Run git submodule update --init --recursive "
        "or explicitly configure with -DSIGN_EMBED_OPENSC=OFF.")
endif()
set(SIGN_OPENSC_PREFIX "${PROJECT_BINARY_DIR}/opensc/install")
set(SIGN_OPENSC_ARCHIVE "${SIGN_OPENSC_PREFIX}/lib/libcrsign-opensc.a")
file(GLOB_RECURSE SIGN_OPENSC_SOURCES CONFIGURE_DEPENDS
    "${SIGN_OPENSC_SOURCE}/*")
list(FILTER SIGN_OPENSC_SOURCES EXCLUDE REGEX "/\\.git(/|$)")
set(SIGN_OPENSC_BUILD_ARGS)
if(APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET)
    list(APPEND SIGN_OPENSC_BUILD_ARGS
        --deployment-target "${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()
add_custom_command(OUTPUT "${SIGN_OPENSC_ARCHIVE}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/build-opensc.py"
        --source "${SIGN_OPENSC_SOURCE}" --build "${PROJECT_BINARY_DIR}/opensc"
        --prefix "${SIGN_OPENSC_PREFIX}" ${SIGN_OPENSC_BUILD_ARGS}
    DEPENDS "${PROJECT_SOURCE_DIR}/scripts/build-opensc.py" ${SIGN_OPENSC_SOURCES}
    COMMENT "Building embedded OpenSC from vendor/opensc (offline)"
    VERBATIM)
add_custom_target(sign_opensc_build DEPENDS "${SIGN_OPENSC_ARCHIVE}")
add_library(sign_opensc_deps STATIC IMPORTED GLOBAL)
set_target_properties(sign_opensc_deps PROPERTIES
    IMPORTED_LOCATION "${SIGN_OPENSC_ARCHIVE}")
add_dependencies(sign_opensc_deps sign_opensc_build)
target_compile_definitions(sign_opensc_deps INTERFACE SIGN_EMBED_OPENSC)
find_package(Threads REQUIRED)
target_link_libraries(sign_opensc_deps INTERFACE
    OpenSSL::Crypto Threads::Threads ${CMAKE_DL_LIBS})
if(WIN32)
    target_link_libraries(sign_opensc_deps INTERFACE ws2_32 shlwapi)
endif()
install(FILES "${SIGN_OPENSC_SOURCE}/COPYING"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/crsign-deps" RENAME opensc-COPYING)
