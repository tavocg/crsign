# SPDX-License-Identifier: GPL-3.0-or-later
# glibc supplies argp; other C libraries can use argp-standalone.
include(CheckCSourceCompiles)
include(FindPackageHandleStandardArgs)

# An explicit ARGP_LIBRARY selects an external implementation even on glibc.
if(NOT ARGP_LIBRARY)
    check_c_source_compiles("#include <argp.h>
        int main(int argc, char **argv) {
            struct argp parser = {0};
            return argp_parse(&parser, argc, argv, 0, 0, 0);
        }" ARGP_IN_LIBC)
endif()

if(ARGP_IN_LIBC AND NOT ARGP_LIBRARY)
    set(ARGP_AVAILABLE TRUE)
else()
    find_path(ARGP_INCLUDE_DIR NAMES argp.h)
    find_library(ARGP_LIBRARY NAMES argp argp-standalone)
    if(ARGP_INCLUDE_DIR AND ARGP_LIBRARY)
        set(ARGP_AVAILABLE TRUE)
    endif()
endif()
find_package_handle_standard_args(Argp REQUIRED_VARS ARGP_AVAILABLE
    REASON_FAILURE_MESSAGE "Install argp-standalone and set ARGP_INCLUDE_DIR/ARGP_LIBRARY if needed.")

if(Argp_FOUND AND NOT TARGET Argp::Argp)
    add_library(Argp::Argp INTERFACE IMPORTED)
    if(ARGP_LIBRARY)
        set_target_properties(Argp::Argp PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${ARGP_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "${ARGP_LIBRARY}")
    endif()
endif()
mark_as_advanced(ARGP_INCLUDE_DIR ARGP_LIBRARY)
