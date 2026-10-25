/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/file_state.h>
#include <sign/history.h>
#include <sign/user_data.h>
#include <string.h>
struct sign_history {
	GArray* entries;
};
char* history_path(void) {
	return g_build_filename(
	    g_get_user_data_dir(),
	    "crsign",
	    "history.json",
	    NULL
	);
}
static bool invalid(GError** error, const char* message) {
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, message);
	return false;
}
static gint newest(gconstpointer a, gconstpointer b) {
	const sign_history_entry* x = a;
	const sign_history_entry* y = b;
	if (x->signed_at != y->signed_at) {
		return x->signed_at > y->signed_at ? -1 : 1;
	}
	return strcmp(x->path, y->path);
}
void history_free(sign_history* history) {
	if (!history) {
		return;
	}
	for (guint n = 0; n < history->entries->len; ++n) {
		g_free(
		    (char*)g_array_index(history->entries, sign_history_entry, n).path
		);
	}
	g_array_unref(history->entries);
	g_free(history);
}
void history_prune_missing(sign_history* history) {
	for (guint n = history->entries->len; n > 0; --n) {
		sign_history_entry* entry = &g_array_index(
		    history->entries,
		    sign_history_entry,
		    n - 1
		);
		GFile* file = g_file_new_for_path(entry->path);
		if (file_is_missing(file)) {
			g_free((char*)entry->path);
			g_array_remove_index(history->entries, n - 1);
		}
		g_object_unref(file);
	}
}
size_t history_count(const sign_history* h) {
	return h ? h->entries->len : 0;
}
const sign_history_entry* history_get(const sign_history* h, size_t n) {
	return h && n < h->entries->len
	           ? &g_array_index(h->entries, sign_history_entry, n)
	           : NULL;
}
sign_history* history_load(GError** error) {
	sign_history* h = g_new0(sign_history, 1);
	h->entries = g_array_new(FALSE, FALSE, sizeof(sign_history_entry));
	GError* cause = NULL;
	json_t* root = user_data_load(
	    "history.json",
	    SIGN_HISTORY_MAX_SIZE,
	    &cause
	);
	if (!root) {
		if (g_error_matches(cause, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
			g_clear_error(&cause);
			return h;
		}
		g_propagate_error(error, cause);
		history_free(h);
		return NULL;
	}
	json_t* entries = json_object_get(root, "documents");
	json_t* version = json_object_get(root, "version");
	bool    ok = json_is_integer(version) && json_integer_value(version) == 1 &&
	          json_is_array(entries);
	GHashTable* seen = g_hash_table_new(g_str_hash, g_str_equal);
	size_t      index;
	json_t*     object;
	if (ok) {
		json_array_foreach(entries, index, object) {
			json_t*     path_value = json_object_get(object, "path");
			const char* text = json_string_value(path_value);
			json_t*     date = json_object_get(object, "signed_at");
			json_t*     favorite = json_object_get(object, "favorite");
			if (!text || !*text ||
			    strlen(text) != json_string_length(path_value) ||
			    !g_path_is_absolute(text) || !json_is_integer(date) ||
			    json_integer_value(date) <= 0 || !json_is_boolean(favorite) ||
			    g_hash_table_contains(seen, text)) {
				ok = false;
				break;
			}
			sign_history_entry entry = {
			    g_strdup(text),
			    (gint64)json_integer_value(date),
			    json_is_true(favorite)
			};
			g_array_append_val(h->entries, entry);
			g_hash_table_add(seen, (gpointer)entry.path);
		}
	}
	g_hash_table_unref(seen);
	json_decref(root);
	if (!ok) {
		invalid(error, "Invalid history format or version");
		history_free(h);
		return NULL;
	}
	g_array_sort(h->entries, newest);
	return h;
}
static bool save_history(sign_history* h, GError** error) {
	json_t* array = json_array();
	for (guint n = 0; n < h->entries->len; ++n) {
		const sign_history_entry* entry = history_get(h, n);
		json_array_append_new(
		    array,
		    json_pack(
		        "{s:s,s:I,s:b}",
		        "path",
		        entry->path,
		        "signed_at",
		        (json_int_t)entry->signed_at,
		        "favorite",
		        entry->favorite
		    )
		);
	}
	json_t* root = json_pack("{s:i,s:o}", "version", 1, "documents", array);
	bool
	    ok = user_data_save("history.json", root, SIGN_HISTORY_MAX_SIZE, error);
	json_decref(root);
	return ok;
}

static bool update(
    const char* output,
    bool        record,
    bool        favorite,
    GError**    error
) {
	if (!output || !*output || !g_utf8_validate(output, -1, NULL)) {
		return invalid(error, "Invalid history path");
	}
	char* path = g_canonicalize_filename(output, NULL);
	int   fd = user_data_lock(error);
	bool  ok = false;
	if (fd >= 0) {
		sign_history* h = history_load(error);
		if (h) {
			sign_history_entry* entry = NULL;
			for (guint n = 0; n < h->entries->len; ++n) {
				sign_history_entry* candidate = &g_array_index(
				    h->entries,
				    sign_history_entry,
				    n
				);
				if (!strcmp(candidate->path, path)) {
					entry = candidate;
					break;
				}
			}
			if (record && !entry) {
				sign_history_entry item =
				    {g_strdup(path), g_get_real_time(), false};
				g_array_append_val(h->entries, item);
			} else if (entry) {
				if (record) {
					entry->signed_at = g_get_real_time();
				} else {
					entry->favorite = favorite;
				}
			} else {
				g_set_error_literal(
				    error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_FOUND,
				    "Document is not in history"
				);
			}
			if (record || entry) {
				g_array_sort(h->entries, newest);
				ok = save_history(h, error);
			}
			history_free(h);
		}
		user_data_unlock(fd);
	}
	g_free(path);
	return ok;
}
bool history_record(const char* output, GError** error) {
	return update(output, true, false, error);
}
bool history_favorite(const char* output, bool favorite, GError** error) {
	return update(output, false, favorite, error);
}
