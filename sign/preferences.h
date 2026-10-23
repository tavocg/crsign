/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_PREFERENCES_H
#define SIGN_PREFERENCES_H
#include <gio/gio.h>
#include <stdbool.h>

typedef struct sign_preferences sign_preferences;
/* Sections store explicit values only; absent keys inherit defaults/globals.
 * Empty values explicitly restore automatic/default selection.
 * Values are UTF-8 strings, booleans or arrays of strings. */
typedef GHashTable        sign_preferences_section;
sign_preferences*         preferences_load(GError** error);
void                      preferences_free(sign_preferences* preferences);
const char*               preferences_path(const sign_preferences* preferences);
sign_preferences_section* preferences_global(sign_preferences* preferences);
size_t preferences_count(const sign_preferences* preferences);
sign_preferences_section* preferences_profile(
    sign_preferences* preferences,
    size_t            index
);
sign_preferences_section* preferences_add(sign_preferences* preferences);
void      preferences_remove(sign_preferences* preferences, size_t index);
GVariant* preferences_get(sign_preferences_section* section, const char* key);
/* Takes a reference to value; NULL removes a setting. Floating values consumed.
 */
void preferences_set(
    sign_preferences_section* section,
    const char*               key,
    GVariant*                 value
);
/* Validates every profile before atomically replacing the file. Refuses to
 * replace externally changed files. Missing directories created only on save.
 * Empty global settings are omitted; explicit empty profile overrides remain.
 * Comments/layout are normalized; all supported settings are retained. */
bool preferences_save(sign_preferences* preferences, GError** error);
#endif
