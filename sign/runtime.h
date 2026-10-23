/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_RUNTIME_H
#define SIGN_RUNTIME_H
#include <stdbool.h>
/* Locate resources only for marked portable installations. */
bool runtime_init(void);
/* Resolve a path against the executable installation prefix; free with
 * g_free(). */
char* runtime_install_path(const char* path);
#endif
