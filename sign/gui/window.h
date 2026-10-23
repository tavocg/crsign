/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_GUI_WINDOW_H
#define SIGN_GUI_WINDOW_H
#include <adwaita.h>

#define SIGN_TYPE_WINDOW (sign_window_get_type())
G_DECLARE_FINAL_TYPE(
    SignWindow,
    sign_window,
    SIGN,
    WINDOW,
    AdwApplicationWindow
)

GtkWidget* sign_window_new(GtkApplication* application);
#endif
