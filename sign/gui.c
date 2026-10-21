/*
 * This file is part of CRSign.
 * Copyright (C) 2026 Gustavo Calvo <tavo@tavo.cr>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * See sign/main.c for the full copyright and warranty notice.
 * See LICENSE in the project root for the GNU General Public License.
 */

#include <gtk/gtk.h>
#include <sign/gui.h>

static const char APPLICATION_ID[] = "org.crsign.Sign";
static const char WINDOW_TITLE[] = "Sign";

static void activate(GtkApplication* app, gpointer user_data) {
	(void)user_data;
	GtkWidget* window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window), WINDOW_TITLE);
	gtk_window_set_default_size(GTK_WINDOW(window), 320, 160);
	gtk_window_set_child(GTK_WINDOW(window), gtk_label_new("Hello, world!"));
	gtk_window_present(GTK_WINDOW(window));
}

int gui_run(char* program_name) {
	g_set_application_name("Sign");
	char*           argv[] = {program_name, NULL};
	GtkApplication* app = gtk_application_new(
	    APPLICATION_ID,
	    G_APPLICATION_DEFAULT_FLAGS
	);

	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	int status = g_application_run(G_APPLICATION(app), 1, argv);
	g_object_unref(app);

	return status;
}
