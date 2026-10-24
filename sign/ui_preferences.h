/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_UI_PREFERENCES_H
#define SIGN_UI_PREFERENCES_H
#include <gio/gio.h>
#include <stdbool.h>
bool ui_preferences_grid(bool* grid, GError**);
bool ui_preferences_set_grid(bool grid, GError**);
#endif
