/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_GUI_SETTINGS_H
#define SIGN_GUI_SETTINGS_H
#include <adwaita.h>
#include <stdbool.h>
#define SIGN_TYPE_SETTINGS (sign_settings_get_type())
G_DECLARE_FINAL_TYPE(SignSettings, sign_settings, SIGN, SETTINGS, GtkBox)
GtkWidget* sign_settings_new(void);
bool       sign_settings_flush(SignSettings* self);
#endif
