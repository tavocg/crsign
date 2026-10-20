cmake_minimum_required(VERSION 3.16)
project(crsign VERSION 0.1.0 LANGUAGES C)

option(HEADLESS "Build without the GTK graphical interface" OFF)

add_executable(sign sign/main.c)
target_compile_features(sign PRIVATE c_std_99)
target_compile_definitions(sign PRIVATE SIGN_VERSION="${PROJECT_VERSION}")

if(NOT HEADLESS)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
    target_compile_definitions(sign PRIVATE SIGN_WITH_GUI)
    target_link_libraries(sign PRIVATE PkgConfig::GTK4)
endif()

install(TARGETS sign RUNTIME DESTINATION bin)
