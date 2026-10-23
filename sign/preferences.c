/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <glib/gstdio.h>
#include <sign/config.h>
#include <sign/preferences.h>
#include <string.h>
#include <tomlc17.h>

struct sign_preferences {
	char*                     path;
	char*                     etag;
	bool                      existed;
	sign_preferences_section* global;
	GPtrArray*                profiles;
};
static sign_preferences_section* section_new(void) {
	return g_hash_table_new_full(
	    g_str_hash,
	    g_str_equal,
	    g_free,
	    (GDestroyNotify)g_variant_unref
	);
}
GVariant* preferences_get(sign_preferences_section* section, const char* key) {
	return g_hash_table_lookup(section, key);
}
void preferences_set(
    sign_preferences_section* section,
    const char*               key,
    GVariant*                 value
) {
	if (value) {
		g_hash_table_replace(section, g_strdup(key), g_variant_ref_sink(value));
	} else {
		g_hash_table_remove(section, key);
	}
}
static void section_read(
    sign_preferences_section* section,
    toml_datum_t              table,
    const char*               prefix
) {
	for (int i = 0; i < table.u.tab.size; ++i) {
		const char* name = table.u.tab.key[i];
		if (!prefix[0] &&
		    (!strcmp(name, "version") || !strcmp(name, "profiles"))) {
			continue;
		}
		char*        key = g_strconcat(prefix, name, NULL);
		toml_datum_t value = table.u.tab.value[i];
		if (value.type == TOML_TABLE) {
			char* nested = g_strconcat(key, ".", NULL);
			section_read(section, value, nested);
			g_free(nested);
		} else if (value.type == TOML_STRING) {
			preferences_set(section, key, g_variant_new_string(value.u.s));
		} else if (value.type == TOML_BOOLEAN) {
			preferences_set(
			    section,
			    key,
			    g_variant_new_boolean(value.u.boolean)
			);
		} else if (value.type == TOML_ARRAY) {
			GVariantBuilder array;
			g_variant_builder_init(&array, G_VARIANT_TYPE_STRING_ARRAY);
			for (int j = 0; j < value.u.arr.size; ++j) {
				g_variant_builder_add(&array, "s", value.u.arr.elem[j].u.s);
			}
			preferences_set(section, key, g_variant_builder_end(&array));
		}
		g_free(key);
	}
}
sign_preferences* preferences_load(GError** error) {
	sign_preferences* p = g_new0(sign_preferences, 1);
	p->path = config_path();
	p->global = section_new();
	p->profiles = g_ptr_array_new_with_free_func(
	    (GDestroyNotify)g_hash_table_unref
	);
	GFile*  file = g_file_new_for_path(p->path);
	char*   source = NULL;
	gsize   len = 0;
	GError* cause = NULL;
	p->existed = g_file_load_contents(
	    file,
	    NULL,
	    &source,
	    &len,
	    &p->etag,
	    &cause
	);
	g_object_unref(file);
	if (!p->existed) {
		if (g_error_matches(cause, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
			g_clear_error(&cause);
			return p;
		}
		g_propagate_error(error, cause);
		preferences_free(p);
		return NULL;
	}
	char  detail[512];
	char* directory = g_path_get_dirname(p->path);
	bool  ok = config_validate_source(
        source,
        len,
        directory,
        detail,
        sizeof(detail)
    );
	g_free(directory);
	if (ok) {
		toml_result_t parsed = toml_parse(source, (int)len);
		section_read(p->global, parsed.toptab, "");
		toml_datum_t profiles = toml_get(parsed.toptab, "profiles");
		for (int i = 0; profiles.type == TOML_ARRAY && i < profiles.u.arr.size;
		     ++i) {
			sign_preferences_section* section = section_new();
			section_read(section, profiles.u.arr.elem[i], "");
			g_ptr_array_add(p->profiles, section);
		}
		toml_free(parsed);
	} else {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, detail);
	}
	g_free(source);
	if (!ok) {
		preferences_free(p);
		return NULL;
	}
	return p;
}
void preferences_free(sign_preferences* p) {
	if (!p) {
		return;
	}
	g_free(p->path);
	g_free(p->etag);
	g_hash_table_unref(p->global);
	g_ptr_array_unref(p->profiles);
	g_free(p);
}
const char* preferences_path(const sign_preferences* p) {
	return p->path;
}
sign_preferences_section* preferences_global(sign_preferences* p) {
	return p->global;
}
size_t preferences_count(const sign_preferences* p) {
	return p->profiles->len;
}
sign_preferences_section* preferences_profile(
    sign_preferences* p,
    size_t            index
) {
	return index < p->profiles->len ? g_ptr_array_index(p->profiles, index)
	                                : NULL;
}
sign_preferences_section* preferences_add(sign_preferences* p) {
	sign_preferences_section* section = section_new();
	g_ptr_array_add(p->profiles, section);
	return section;
}
void preferences_remove(sign_preferences* p, size_t index) {
	if (index < p->profiles->len) {
		g_ptr_array_remove_index(p->profiles, index);
	}
}
static void quoted(GString* text, const char* value) {
	g_string_append_c(text, '"');
	for (const unsigned char* c = (const unsigned char*)value; *c; ++c) {
		switch (*c) {
		case '"':
			g_string_append(text, "\\\"");
			break;
		case '\\':
			g_string_append(text, "\\\\");
			break;
		case '\n':
			g_string_append(text, "\\n");
			break;
		case '\r':
			g_string_append(text, "\\r");
			break;
		case '\t':
			g_string_append(text, "\\t");
			break;
		default:
			if (*c < 32 || *c == 127) {
				g_string_append_printf(text, "\\u%04x", *c);
			} else {
				g_string_append_c(text, *c);
			}
		}
	}
	g_string_append_c(text, '"');
}
static gboolean empty_setting(gpointer key, gpointer data, gpointer user_data) {
	(void)key;
	(void)user_data;
	GVariant* value = data;
	return (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING) &&
	        !g_variant_get_string(value, NULL)[0]) ||
	       (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING_ARRAY) &&
	        g_variant_n_children(value) == 0);
}
static bool section_write(
    GString*                  text,
    sign_preferences_section* section,
    bool                      omit_empty,
    GError**                  error
) {
	GList* keys = g_hash_table_get_keys(section);
	keys = g_list_sort(keys, (GCompareFunc)g_strcmp0);
	bool ok = true;
	for (GList* node = keys; node && ok; node = node->next) {
		const char* key = node->data;
		/* Key names never contain user data; validation rejects unknown keys.
		 */
		if (strspn(key, "abcdefghijklmnopqrstuvwxyz_.") != strlen(key)) {
			g_set_error_literal(
			    error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "Invalid setting name"
			);
			ok = false;
			break;
		}
		GVariant* value = preferences_get(section, key);
		if (omit_empty && empty_setting(NULL, value, NULL)) {
			continue;
		}
		g_string_append_printf(text, "%s = ", key);
		if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
			quoted(text, g_variant_get_string(value, NULL));
		} else if (g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)) {
			g_string_append(
			    text,
			    g_variant_get_boolean(value) ? "true" : "false"
			);
		} else if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING_ARRAY)) {
			g_string_append_c(text, '[');
			GVariantIter iter;
			const char*  item;
			size_t       i = 0;
			g_variant_iter_init(&iter, value);
			while (g_variant_iter_next(&iter, "&s", &item)) {
				if (i++) {
					g_string_append(text, ", ");
				}
				quoted(text, item);
			}
			g_string_append_c(text, ']');
		} else {
			g_set_error_literal(
			    error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "Invalid setting type"
			);
			ok = false;
		}
		g_string_append_c(text, '\n');
	}
	g_list_free(keys);
	return ok;
}
static bool serialize(
    sign_preferences* p,
    GString*          text,
    bool              normalize,
    GError**          error
) {
	g_string_assign(text, "version = 0.1\n");
	bool ok = section_write(text, p->global, normalize, error);
	for (guint i = 0; ok && i < p->profiles->len; ++i) {
		g_string_append(text, "\n[[profiles]]\n");
		ok = section_write(
		    text,
		    g_ptr_array_index(p->profiles, i),
		    false,
		    error
		);
	}
	return ok;
}
bool preferences_save(sign_preferences* p, GError** error) {
	GString* text = g_string_new(NULL);
	/* Validate explicit values before normalizing, so invalid or unknown empty
	 * settings are reported rather than silently discarded. */
	bool  ok = serialize(p, text, false, error);
	char* directory = g_path_get_dirname(p->path);
	char  detail[512];
	if (ok && !config_validate_source(
	              text->str,
	              text->len,
	              directory,
	              detail,
	              sizeof(detail)
	          )) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, detail);
		ok = false;
	}
	if (ok) {
		ok = serialize(p, text, true, error);
	}
	GFile* file = g_file_new_for_path(p->path);
	if (ok && !p->existed && g_file_query_exists(file, NULL)) {
		g_set_error_literal(
		    error,
		    G_IO_ERROR,
		    G_IO_ERROR_WRONG_ETAG,
		    "Configuration was created externally; reload before saving"
		);
		ok = false;
	}
	if (ok) {
		GFile*  parent = g_file_get_parent(file);
		GError* cause = NULL;
		ok = g_file_make_directory_with_parents(parent, NULL, &cause);
		if (!ok && g_error_matches(cause, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
			ok = true;
			g_clear_error(&cause);
		}
		if (!ok) {
			g_propagate_error(error, cause);
		}
		g_object_unref(parent);
	}
	if (ok) {
		char* etag = NULL;
		ok = g_file_replace_contents(
		    file,
		    text->str,
		    text->len,
		    p->etag,
		    FALSE,
		    G_FILE_CREATE_PRIVATE | G_FILE_CREATE_REPLACE_DESTINATION,
		    &etag,
		    NULL,
		    error
		);
		if (ok) {
			g_free(p->etag);
			p->etag = etag;
			p->existed = true;
			g_hash_table_foreach_remove(p->global, empty_setting, NULL);
		}
	}
	g_object_unref(file);
	g_free(directory);
	g_string_free(text, TRUE);
	return ok;
}
