/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <gio/gio.h>
#include <sign/gui/i18n.h>
#include <string.h>

int main(int argc, char** argv) {
	g_assert_true(argc == 2 || argc == 3);
	gui_i18n_init();
	if (argc == 3) {
		GFile* actual = g_file_new_for_path(
		    bindtextdomain(GETTEXT_PACKAGE, NULL)
		);
		GFile* expected = g_file_new_for_path(argv[2]);
		g_assert_true(g_file_equal(actual, expected));
		g_object_unref(actual);
		g_object_unref(expected);
	}
	if (!strcmp(argv[1], "es")) {
		g_assert_cmpstr(_("Home"), ==, "Inicio");
		g_assert_cmpstr(_("Sign"), ==, "Firmar");
		g_assert_cmpstr(_("Validate"), ==, "Validar");
		g_assert_cmpstr(_("Settings"), ==, "Ajustes");
		g_assert_cmpstr(_("About"), ==, "Acerca de");
		g_assert_cmpstr(_("Menu"), ==, "Menú");
	} else {
		g_assert_cmpstr(_("Home"), ==, "Home");
		g_assert_cmpstr(_("About"), ==, "About");
		g_assert_cmpstr(_("Menu"), ==, "Menu");
	}
	return 0;
}
