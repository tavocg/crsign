/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <gio/gio.h>
#include <sign/appearance.h>
#include <sign/error.h>
#include <sign/gui/error.h>
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
	for (int code = SIGN_ERROR_NO_TOKEN; code <= SIGN_ERROR_OUTPUT; ++code) {
		GError* error = g_error_new_literal(
		    SIGN_ERROR,
		    code,
		    "documents[0]: internal diagnostic"
		);
		char* text = gui_error_message(error, GUI_ERROR_SIGNING);
		g_assert_nonnull(text);
		g_assert_null(strstr(text, "documents["));
		g_assert_null(strstr(text, "internal diagnostic"));
		if (code == SIGN_ERROR_NO_TOKEN && !strcmp(argv[1], "es")) {
			g_assert_true(g_str_has_prefix(
			    text,
			    "No se encontró ningún dispositivo de firma."
			));
		}
		g_free(text);
		g_error_free(error);
	}
	if (!strcmp(argv[1], "es")) {
		g_assert_cmpstr(_("Home"), ==, "Inicio");
		g_assert_cmpstr(_("Sign"), ==, "Firmar");
		g_assert_cmpstr(_("Validate"), ==, "Validar");
		g_assert_cmpstr(_("Devices"), ==, "Dispositivos");
		g_assert_cmpstr(
		    _("No devices found"),
		    ==,
		    "No se encontraron dispositivos"
		);
		g_assert_cmpstr(_("Settings"), ==, "Ajustes");
		g_assert_cmpstr(_("About"), ==, "Acerca de");
		g_assert_cmpstr(_("Menu"), ==, "Menú");
	} else {
		g_assert_cmpstr(_("Home"), ==, "Home");
		g_assert_cmpstr(_("About"), ==, "About");
		g_assert_cmpstr(_("Menu"), ==, "Menu");
	}
	char* svg = appearance_text_svg("Ana & <CR>");
	g_assert_nonnull(svg);
	g_assert_nonnull(strstr(svg, "Ana &amp; &lt;CR&gt;"));
	if (!strcmp(argv[1], "es")) {
		g_assert_nonnull(strstr(svg, "Firmado digitalmente"));
		g_assert_nonnull(
		    strstr(svg, "Representación gráfica. Verifique la validez.")
		);
	} else {
		g_assert_nonnull(strstr(svg, "Digitally signed"));
		g_assert_nonnull(
		    strstr(svg, "Graphical representation. Verify validity.")
		);
	}
	g_free(svg);
	return 0;
}
