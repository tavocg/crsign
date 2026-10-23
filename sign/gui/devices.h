/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_GUI_DEVICES_H
#define SIGN_GUI_DEVICES_H
#include <adwaita.h>
#include <stdbool.h>
#define SIGN_TYPE_DEVICES (sign_devices_get_type())
G_DECLARE_FINAL_TYPE(SignDevices, sign_devices, SIGN, DEVICES, GtkBox)
GtkWidget* sign_devices_new(void);
void       sign_devices_refresh(SignDevices* self);
bool       sign_devices_is_busy(SignDevices* self);
#endif
