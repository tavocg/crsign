/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_USER_DATA_H
#define SIGN_USER_DATA_H
#include <gio/gio.h>
#include <jansson.h>
#include <stdbool.h>
char*   user_data_path(const char* name);
json_t* user_data_load(const char* name, size_t limit, GError**);
bool    user_data_save(const char* name, json_t*, size_t limit, GError**);
/* Serializes mutations across threads and processes. Reads use atomic files. */
int  user_data_lock(GError**);
void user_data_unlock(int);
#endif
