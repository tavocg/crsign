/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_GUI_HOME_H
#define SIGN_GUI_HOME_H
#include <adwaita.h>
#include <stdbool.h>
#define SIGN_TYPE_HOME (sign_home_get_type())
G_DECLARE_FINAL_TYPE(SignHome, sign_home, SIGN, HOME, GtkBox)
GtkWidget* sign_home_new(void);
void       sign_home_reload(SignHome*);
void       sign_home_set_grid(SignHome*, bool);
GtkWidget* sign_home_search_button(SignHome*);
bool       sign_home_has_documents(SignHome*);
bool       sign_home_is_grid(SignHome*);
#endif
