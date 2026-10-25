/*
 * This file is part of CRSign.
 * Copyright (C) 2026 Gustavo Calvo <tavo@tavo.cr>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * See sign/main.c for the full copyright and warranty notice.
 * See LICENSE in the project root for the GNU General Public License.
 */

#include <adwaita.h>
#include <sign/gui.h>
#include <sign/gui/i18n.h>
#include <sign/gui/window.h>

static const char APPLICATION_ID[] = "org.crsign.Sign";

static void activate(GtkApplication* app, gpointer user_data) {
	(void)user_data;
	gtk_window_set_default_icon_name(APPLICATION_ID);
	GtkWindow* window = gtk_application_get_active_window(app);
	if (!window) {
		window = GTK_WINDOW(sign_window_new(app));
	}
	gtk_window_present(window);
}

int gui_run(char* program_name) {
	gui_i18n_init();
	g_set_application_name("CRSign");
	char*           argv[] = {program_name, NULL};
	AdwApplication* app = adw_application_new(
	    APPLICATION_ID,
	    G_APPLICATION_DEFAULT_FLAGS
	);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	int status = g_application_run(G_APPLICATION(app), 1, argv);
	g_object_unref(app);
	return status;
}
