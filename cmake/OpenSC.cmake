# SPDX-License-Identifier: GPL-3.0-or-later
# Built offline from the archive prepared by Nix or bootstrap-native.py.
find_library(SIGN_OPENSC_LIBRARY NAMES crsign-opensc REQUIRED)
add_library(sign_opensc_deps STATIC IMPORTED GLOBAL)
set_target_properties(sign_opensc_deps PROPERTIES
    IMPORTED_LOCATION "${SIGN_OPENSC_LIBRARY}")
find_package(Threads REQUIRED)
target_link_libraries(sign_opensc_deps INTERFACE
    OpenSSL::Crypto Threads::Threads ${CMAKE_DL_LIBS})
if(WIN32)
    target_link_libraries(sign_opensc_deps INTERFACE ws2_32 shlwapi)
endif()
