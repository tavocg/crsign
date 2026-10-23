/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_GUI_ERROR_H
#define SIGN_GUI_ERROR_H
#include <gtk/gtk.h>
#define SIGN_GUI_ERROR (g_quark_from_static_string("sign-gui-translated-error"))
typedef enum {
	GUI_ERROR_SIGNING,
	GUI_ERROR_VALIDATION,
	GUI_ERROR_SETTINGS,
	GUI_ERROR_DEVICES,
	GUI_ERROR_PDF,
	GUI_ERROR_FILES
} gui_error_context;
const char* gui_trust_error(int);
const char* gui_device_error(unsigned long);
char*       gui_error_message(const GError*, gui_error_context);
void        gui_error_dialog(GtkWindow*, const char*, const char*);
#endif
