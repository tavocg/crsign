/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_HISTORY_H
#define SIGN_HISTORY_H
#include <gio/gio.h>
#include <stdbool.h>
#define SIGN_HISTORY_MAX_SIZE (16u * 1024u * 1024u)
typedef struct sign_history sign_history;
typedef struct {
	const char* path;
	gint64      signed_at; /* UTC microseconds since the Unix epoch. */
	bool        favorite;
} sign_history_entry;
char*         history_path(void);
sign_history* history_load(GError**);
void          history_free(sign_history*);
/* Filter missing entries from this loaded view, keeping saved history intact.
 */
void                      history_prune_missing(sign_history*);
size_t                    history_count(const sign_history*);
const sign_history_entry* history_get(const sign_history*, size_t);
/* Merge under a process/thread lock and atomically replace the JSON. A failed
 * history write never changes a successful signature into a signing failure. */
bool history_record(const char* output, GError**);
bool history_favorite(const char* output, bool favorite, GError**);
#endif
