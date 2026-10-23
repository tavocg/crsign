/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <locale.h>
#include <sign/gui/config.h>
#include <sign/gui/i18n.h>
#include <sign/runtime.h>

void gui_i18n_init(void) {
	setlocale(LC_ALL, "");
	char*       installed = runtime_install_path(SIGN_LOCALE_DIR);
	const char* directory = SIGN_INSTALL_LOCALE_DIR;
	if (installed && g_file_test(installed, G_FILE_TEST_IS_DIR)) {
		directory = installed;
	} else if (g_file_test(SIGN_BUILD_LOCALE_DIR, G_FILE_TEST_IS_DIR)) {
		/* Run directly from the build tree, without installing first. */
		directory = SIGN_BUILD_LOCALE_DIR;
	}
	bindtextdomain(GETTEXT_PACKAGE, directory);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
	textdomain(GETTEXT_PACKAGE);
	g_free(installed);
}
