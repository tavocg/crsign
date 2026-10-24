/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_GUI_FILE_ACTIONS_H
#define SIGN_GUI_FILE_ACTIONS_H
#include <gtk/gtk.h>
#include <stdbool.h>
typedef void (*sign_favorite_changed)(GFile*, bool, gpointer);
void sign_file_actions_attach(
    GtkWidget*,
    GFile*,
    bool,
    bool,
    sign_favorite_changed,
    gpointer
);
/* Add the Validate entry to this file menu (Home and signing views). */
void sign_file_allow_validation(GtkWidget*);
/* Enable exporting the file by drag; keep PDF editing gestures independent. */
void       sign_file_drag_attach(GtkWidget*, GFile*);
GtkWidget* sign_file_location_button(void);
GtkWidget* sign_file_favorite_button(bool);
#endif
