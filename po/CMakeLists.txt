# SPDX-License-Identifier: GPL-3.0-or-later
find_package(Gettext REQUIRED)
find_program(XGETTEXT_EXECUTABLE xgettext REQUIRED)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/LINGUAS" SIGN_LANGUAGES
    REGEX "^[a-zA-Z_@.-]+$")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/LINGUAS")
set(SIGN_CATALOGS)
set(SIGN_UPDATE_COMMANDS
    COMMAND "${XGETTEXT_EXECUTABLE}" --from-code=UTF-8 --language=C
        --keyword=_ --keyword=N_ --add-comments=Translators
        --package-name=CRSign --package-version=${PROJECT_VERSION}
        --msgid-bugs-address=https://github.com/tavocg/crsign/issues
        --copyright-holder=Gustavo\ Calvo
        --files-from=po/POTFILES.in --output=po/crsign.pot)
foreach(language IN LISTS SIGN_LANGUAGES)
    set(catalog "${PROJECT_BINARY_DIR}/locale/${language}/LC_MESSAGES/crsign.mo")
    add_custom_command(OUTPUT "${catalog}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${PROJECT_BINARY_DIR}/locale/${language}/LC_MESSAGES"
        COMMAND "${GETTEXT_MSGFMT_EXECUTABLE}" --check
            -o "${catalog}" "${CMAKE_CURRENT_SOURCE_DIR}/${language}.po"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${language}.po"
        VERBATIM)
    list(APPEND SIGN_CATALOGS "${catalog}")
    install(FILES "${catalog}"
        DESTINATION "${CMAKE_INSTALL_LOCALEDIR}/${language}/LC_MESSAGES")
    list(APPEND SIGN_UPDATE_COMMANDS COMMAND "${GETTEXT_MSGMERGE_EXECUTABLE}"
        --update --backup=none "po/${language}.po" po/crsign.pot)
endforeach()
add_custom_target(translations ALL DEPENDS ${SIGN_CATALOGS})
# Source catalogs change only when a developer explicitly requests this target.
add_custom_target(update-translations ${SIGN_UPDATE_COMMANDS}
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" VERBATIM)
