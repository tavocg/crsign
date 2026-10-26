/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_GUI_ICONS_H
#define SIGN_GUI_ICONS_H
#include <gtk/gtk.h>
static inline GtkWidget* sign_clear_button_new(void) {
	GtkIconTheme* theme = gtk_icon_theme_get_for_display(
	    gdk_display_get_default()
	);
	if (!g_object_get_data(G_OBJECT(theme), "crsign-icons")) {
		gtk_icon_theme_add_resource_path(theme, "/org/crsign/Sign/icons");
		g_object_set_data(G_OBJECT(theme), "crsign-icons", GINT_TO_POINTER(1));
	}
	return gtk_button_new_from_icon_name("crsign-clear-symbolic");
}
#endif
