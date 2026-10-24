/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/ui_preferences.h>
#include <sign/user_data.h>
#include <string.h>
#define MAX_PREFERENCES_SIZE (64u * 1024u)
static json_t* load(GError** error) {
	GError* cause = NULL;
	json_t* root = user_data_load(
	    "preferences.json",
	    MAX_PREFERENCES_SIZE,
	    &cause
	);
	if (!root) {
		if (g_error_matches(cause, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
			g_clear_error(&cause);
			return json_pack("{s:i}", "version", 1);
		}
		g_propagate_error(error, cause);
		return NULL;
	}
	json_t *version = json_object_get(root, "version"),
	       *home = json_object_get(root, "home"),
	       *view = json_object_get(home, "view");
	if (!json_is_integer(version) || json_integer_value(version) != 1 ||
	    (home && !json_is_object(home)) ||
	    (view && (!json_is_string(view) ||
	              (strcmp(json_string_value(view), "grid") &&
	               strcmp(json_string_value(view), "list"))))) {
		json_decref(root);
		g_set_error_literal(
		    error,
		    G_IO_ERROR,
		    G_IO_ERROR_INVALID_DATA,
		    "Invalid interface preferences format or version"
		);
		return NULL;
	}
	return root;
}
bool ui_preferences_grid(bool* grid, GError** error) {
	*grid = true;
	json_t* root = load(error);
	if (!root) {
		return false;
	}
	const char* view = json_string_value(
	    json_object_get(json_object_get(root, "home"), "view")
	);
	*grid = !view || strcmp(view, "list");
	json_decref(root);
	return true;
}
bool ui_preferences_set_grid(bool grid, GError** error) {
	int fd = user_data_lock(error);
	if (fd < 0) {
		return false;
	}
	json_t* root = load(error);
	bool    ok = false;
	if (root) {
		json_t* home = json_object_get(root, "home");
		if (!home) {
			home = json_object();
			json_object_set_new(root, "home", home);
		}
		json_object_set_new(home, "view", json_string(grid ? "grid" : "list"));
		ok = user_data_save(
		    "preferences.json",
		    root,
		    MAX_PREFERENCES_SIZE,
		    error
		);
		json_decref(root);
	}
	user_data_unlock(fd);
	return ok;
}
