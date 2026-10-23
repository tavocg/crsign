/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_GUI_FILE_DROP_H
#define SIGN_GUI_FILE_DROP_H
#include <gtk/gtk.h>
#include <stdbool.h>
typedef void (*sign_files_received)(GtkWidget*, GListModel*, gpointer);
typedef GCancellable* (*sign_drop_cancellable)(GtkWidget*, gpointer);
typedef void (*sign_drop_state)(GtkWidget*, bool, const GError*, gpointer);
void sign_file_drop_attach(
    GtkWidget*            widget,
    sign_files_received   received,
    sign_drop_cancellable cancellable,
    sign_drop_state       state,
    gpointer              data
);
#endif
