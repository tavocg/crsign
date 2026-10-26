/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_GUI_DEVICES_H
#define SIGN_GUI_DEVICES_H
#include <adwaita.h>
#include <stdbool.h>
#define SIGN_TYPE_DEVICES (sign_devices_get_type())
G_DECLARE_FINAL_TYPE(SignDevices, sign_devices, SIGN, DEVICES, GtkBox)
GtkWidget*  sign_devices_new(void);
void        sign_devices_refresh(SignDevices* self);
bool        sign_devices_is_busy(SignDevices* self);
GtkWidget*  sign_devices_header_menu(SignDevices*);
const char* sign_devices_title(SignDevices* self);
bool        sign_devices_in_details(SignDevices* self);
void        sign_devices_back(SignDevices* self);
GtkWidget*  sign_devices_header_refresh(SignDevices* self);
#endif
