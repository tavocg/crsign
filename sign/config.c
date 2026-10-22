/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <sign/config.h>
#include <sign/io.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <tomlc17.h>

struct sign_config {
	toml_result_t document;
	GPtrArray*    allocations;
};

/* Schema metadata maps TOML directly onto the shared signing options. */
typedef struct {
	const char* name;
	int         option;
	toml_type_t type;
	bool        path;
} config_field;

static const config_field profile_fields[] = {
    {"name", 0, TOML_STRING, false},
    {"label", 0, TOML_STRING, false},
    {"pincmd", 0, TOML_UNKNOWN, false},
    {"modules", 0, TOML_ARRAY, false},
    {"token", 't', TOML_STRING, false},
    {"cert_id", 'k', TOML_STRING, false},
    {"algorithm", 'a', TOML_STRING, false},
    {"hash", 'H', TOML_STRING, false},
    {"level", 'l', TOML_STRING, false},
    {"format", 'f', TOML_STRING, false},
    {"mime_type", MIME_TYPE, TOML_STRING, false},
    {"detached", DETACHED, TOML_BOOLEAN, false},
    {"ca", CA_FILE, TOML_STRING, true},
    {"intermediates", INTERMEDIATES, TOML_STRING, true},
    {"no_bundled_certs", NO_BUNDLED_CERTS, TOML_BOOLEAN, false},
    {"cert_bundles", CERT_BUNDLE, TOML_ARRAY, false},
    {"tsa", 0, TOML_TABLE, false},
};
static const config_field tsa_fields[] = {
    {"url", 'A', TOML_STRING, false},
    {"ca", TSA_CA_FILE, TOML_STRING, true},
    {"intermediates", TSA_INTERMEDIATES, TOML_STRING, true},
};

static bool config_error(char* error, size_t size, const char* format, ...) {
	va_list ap;
	va_start(ap, format);
	vsnprintf(error, size, format, ap);
	va_end(ap);
	return false;
}

static bool valid_string(toml_datum_t value) {
	return value.type == TOML_STRING &&
	       strlen(value.u.s) == (size_t)value.u.str.len;
}

/* Parsed argv and relative paths share the config's allocation lifetime. */
static bool read_pincmd(
    sign_args*          args,
    struct sign_config* config,
    toml_datum_t        value,
    char*               error,
    size_t              size
) {
	char** argv = NULL;
	if (value.type == TOML_STRING && valid_string(value)) {
		if (!g_shell_parse_argv(value.u.s, NULL, &argv, NULL) || !argv[0][0]) {
			g_strfreev(argv);
			return config_error(
			    error,
			    size,
			    "pincmd must contain a valid command"
			);
		}
		for (size_t i = 0; argv[i]; ++i) {
			g_ptr_array_add(config->allocations, argv[i]);
		}
	} else if (value.type == TOML_ARRAY) {
		for (int i = 0; i < value.u.arr.size; ++i) {
			toml_datum_t item = value.u.arr.elem[i];
			if (!valid_string(item) || (i == 0 && !item.u.s[0])) {
				return config_error(
				    error,
				    size,
				    "pincmd must contain string arguments and a nonempty "
				    "executable"
				);
			}
		}
		if (!value.u.arr.size) {
			args->pincmd = NULL;
			return true;
		}
		argv = g_new0(char*, (size_t)value.u.arr.size + 1);
		for (int i = 0; i < value.u.arr.size; ++i) {
			argv[i] = (char*)value.u.arr.elem[i].u.s;
		}
	} else {
		return config_error(
		    error,
		    size,
		    "pincmd must be a string or an array of strings"
		);
	}
	g_ptr_array_add(config->allocations, argv);
	args->pincmd = (const char* const*)argv;
	return true;
}

/* Module paths use the same owned configuration lifetime as other settings. */
static bool read_modules(
    sign_args*          args,
    struct sign_config* config,
    const char*         directory,
    toml_datum_t        value,
    char*               error,
    size_t              size
) {
	if (value.type != TOML_ARRAY) {
		return config_error(error, size, "modules must be an array of paths");
	}
	const char** paths = g_new0(const char*, (size_t)value.u.arr.size + 1);
	g_ptr_array_add(config->allocations, paths);
	args->modules = (libssig_string_list){.items = paths};
	for (int i = 0; i < value.u.arr.size; ++i) {
		toml_datum_t item = value.u.arr.elem[i];
		if (!valid_string(item) || !item.u.s[0]) {
			return config_error(
			    error,
			    size,
			    "modules must contain nonempty paths"
			);
		}
		char* path = g_canonicalize_filename(item.u.s, directory);
		g_ptr_array_add(config->allocations, path);
		bool duplicate = false;
		for (size_t j = 0; j < args->modules.count; ++j) {
			if (!strcmp(path, paths[j])) {
				duplicate = true;
			}
		}
		if (!duplicate) {
			paths[args->modules.count++] = path;
		}
	}
	return true;
}

static bool read_fields(
    sign_args*          args,
    struct sign_config* config,
    const char*         directory,
    toml_datum_t        table,
    const config_field* fields,
    size_t              count,
    char*               error,
    size_t              size
) {
	for (int i = 0; i < table.u.tab.size; ++i) {
		const char*         key = table.u.tab.key[i];
		const config_field* field = NULL;
		for (size_t j = 0; j < count; ++j) {
			if (strlen(fields[j].name) == (size_t)table.u.tab.len[i] &&
			    !strcmp(key, fields[j].name)) {
				field = &fields[j];
				break;
			}
		}
		if (!field) {
			return config_error(error, size, "Unknown profile field: %s", key);
		}
		toml_datum_t value = table.u.tab.value[i];
		if (!strcmp(field->name, "modules")) {
			if (!read_modules(args, config, directory, value, error, size)) {
				return false;
			}
			continue;
		}
		if (!strcmp(field->name, "pincmd")) {
			if (!read_pincmd(args, config, value, error, size)) {
				return false;
			}
			continue;
		}
		if (value.type != field->type ||
		    (value.type == TOML_STRING && !valid_string(value))) {
			return config_error(
			    error,
			    size,
			    "Invalid type or string for profile field: %s",
			    key
			);
		}
		if (value.type == TOML_TABLE) {
			if (!read_fields(
			        args,
			        config,
			        directory,
			        value,
			        tsa_fields,
			        G_N_ELEMENTS(tsa_fields),
			        error,
			        size
			    )) {
				return false;
			}
			continue;
		}
		if (!field->option) {
			continue;
		}
		if (value.type == TOML_ARRAY) {
			args->cert_bundle_count = 0;
			for (int j = 0; j < value.u.arr.size; ++j) {
				toml_datum_t item = value.u.arr.elem[j];
				if (!valid_string(item)) {
					return config_error(
					    error,
					    size,
					    "cert_bundles must contain strings"
					);
				}
				if (!arg_setting(args, field->option, item.u.s, error, size)) {
					return false;
				}
			}
			continue;
		}
		const char* text = value.type == TOML_BOOLEAN
		                       ? (value.u.boolean ? "true" : "false")
		                       : value.u.s;
		if (!text[0]) {
			return config_error(
			    error,
			    size,
			    "Empty profile field: %s; omit it to use the default",
			    key
			);
		}
		if (field->path && !g_path_is_absolute(text)) {
			char* path = g_build_filename(directory, text, NULL);
			g_ptr_array_add(config->allocations, path);
			text = path;
		}
		if (!arg_setting(args, field->option, text, error, size)) {
			return false;
		}
	}
	return true;
}

/* Version-specific reader: future schemas/migrations can dispatch here. */
static bool read_v01(
    sign_args*          args,
    struct sign_config* config,
    const char*         directory,
    const char*         label,
    char*               error,
    size_t              size
) {
	toml_datum_t root = config->document.toptab;
	for (int i = 0; i < root.u.tab.size; ++i) {
		const char* key = root.u.tab.key[i];
		if (strlen(key) != (size_t)root.u.tab.len[i] ||
		    (strcmp(key, "version") && strcmp(key, "profiles") &&
		     strcmp(key, "pincmd") && strcmp(key, "modules"))) {
			return config_error(
			    error,
			    size,
			    "Unknown configuration field: %s",
			    key
			);
		}
	}
	toml_datum_t modules = toml_get(root, "modules");
	if (modules.type != TOML_UNKNOWN &&
	    !read_modules(args, config, directory, modules, error, size)) {
		return false;
	}
	toml_datum_t command = toml_get(root, "pincmd");
	if (command.type != TOML_UNKNOWN &&
	    !read_pincmd(args, config, command, error, size)) {
		return false;
	}
	if (!label) {
		return true;
	}
	toml_datum_t profiles = toml_get(root, "profiles");
	if (profiles.type != TOML_ARRAY) {
		return config_error(
		    error,
		    size,
		    "profiles must be an array of tables ([[profiles]])"
		);
	}
	toml_datum_t selected = {0};
	GHashTable*  labels = g_hash_table_new(g_str_hash, g_str_equal);
	bool         ok = true;
	for (int i = 0; i < profiles.u.arr.size; ++i) {
		toml_datum_t profile = profiles.u.arr.elem[i];
		toml_datum_t id = profile.type == TOML_TABLE
		                      ? toml_get(profile, "label")
		                      : (toml_datum_t){0};
		if (!valid_string(id) || !id.u.s[0]) {
			ok = config_error(
			    error,
			    size,
			    "profiles[%d] requires a nonempty string label",
			    i
			);
			break;
		}
		if (g_hash_table_contains(labels, id.u.s)) {
			ok = config_error(
			    error,
			    size,
			    "Duplicate profile label: %s",
			    id.u.s
			);
			break;
		}
		g_hash_table_add(labels, (gpointer)id.u.s);
		if (!strcmp(id.u.s, label)) {
			selected = profile;
		}
	}
	g_hash_table_destroy(labels);
	if (!ok) {
		return false;
	}
	if (selected.type != TOML_TABLE) {
		return config_error(error, size, "Unknown profile: %s", label);
	}
	return read_fields(
	    args,
	    config,
	    directory,
	    selected,
	    profile_fields,
	    G_N_ELEMENTS(profile_fields),
	    error,
	    size
	);
}

bool config_load(
    sign_args*  args,
    const char* label,
    char*       error,
    size_t      error_size
) {
	char* directory = g_build_filename(g_get_user_config_dir(), "crsign", NULL);
	char* path = g_build_filename(directory, "crsign.toml", NULL);
	/* A global configuration is optional, but permission/read errors are not
	 * mistaken for an absent file. A requested profile always requires it. */
	if (!label) {
		GStatBuf info;
		if (g_stat(path, &info) < 0 && errno == ENOENT) {
			g_free(path);
			g_free(directory);
			return true;
		}
	}
	sign_buffer source = {0};
	if (!file_load(path, &source, error, error_size)) {
		g_free(directory);
		g_free(path);
		return false;
	}
	struct sign_config* config = g_new0(struct sign_config, 1);
	config->allocations = g_ptr_array_new_with_free_func(g_free);
	config->document = toml_parse((const char*)source.data, (int)source.len);
	free(source.data);
	char      detail[256];
	bool      ok = config->document.ok;
	sign_args resolved = {0};
	if (!ok) {
		g_strlcpy(detail, config->document.errmsg, sizeof(detail));
	} else {
		toml_datum_t version = toml_get(config->document.toptab, "version");
		if ((version.type == TOML_FP64 && version.u.fp64 == 0.1) ||
		    (valid_string(version) && !strcmp(version.u.s, "0.1"))) {
			ok = read_v01(
			    &resolved,
			    config,
			    directory,
			    label,
			    detail,
			    sizeof(detail)
			);
		} else {
			ok = config_error(
			    detail,
			    sizeof(detail),
			    "Missing or unsupported configuration version; expected "
			    "version = 0.1"
			);
		}
	}
	if (ok) {
		config_free(args->config);
		resolved.config = config;
		*args = resolved;
	} else {
		config_error(
		    error,
		    error_size,
		    "%s (profile %s): %s",
		    path,
		    label ? label : "global",
		    detail
		);
		config_free(config);
	}
	g_free(path);
	g_free(directory);
	return ok;
}

void config_free(struct sign_config* config) {
	if (!config) {
		return;
	}
	toml_free(config->document);
	g_ptr_array_free(config->allocations, true);
	g_free(config);
}
