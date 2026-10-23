/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <jansson.h>
#include <math.h>
#include <p11-kit/uri.h>
#include <sign/flow.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

struct sign_flow {
	json_t*       json;
	sign_options* documents;
	size_t        count;
	GPtrArray*    allocations;
};
static gint running;

GQuark sign_flow_error_quark(void) {
	return g_quark_from_static_string("sign-flow-error");
}
static bool invalid(GError** error, const char* path, const char* detail) {
	g_set_error(
	    error,
	    SIGN_FLOW_ERROR,
	    SIGN_FLOW_ERROR_INVALID,
	    "%s: %s",
	    path,
	    detail
	);
	return false;
}

typedef enum { TEXT, PATH, NUMBER, BOOLEAN } field_type;
typedef struct {
	const char* name;
	sign_option option;
	field_type  type;
} field;
static const field document_fields[] = {
    {"input", SIGN_OPTION_INPUT, PATH},
    {"output", SIGN_OPTION_OUTPUT, PATH},
    {"token", SIGN_OPTION_TOKEN, TEXT},
    {"cert_id", SIGN_OPTION_CERT_ID, TEXT},
    {"algorithm", SIGN_OPTION_ALGORITHM, TEXT},
    {"hash", SIGN_OPTION_HASH, TEXT},
    {"level", SIGN_OPTION_LEVEL, TEXT},
    {"format", SIGN_OPTION_FORMAT, TEXT},
    {"mime_type", SIGN_OPTION_MIME_TYPE, TEXT},
    {"detached", SIGN_OPTION_DETACHED, BOOLEAN},
    {"ca", SIGN_OPTION_CA_FILE, PATH},
    {"intermediates", SIGN_OPTION_INTERMEDIATES, PATH},
    {"no_bundled_certs", SIGN_OPTION_NO_BUNDLED_CERTS, BOOLEAN},
};
static const field tsa_fields[] = {
    {"url", SIGN_OPTION_AUTHORITY, TEXT},
    {"ca", SIGN_OPTION_TSA_CA_FILE, PATH},
    {"intermediates", SIGN_OPTION_TSA_INTERMEDIATES, PATH},
};
static const field appearance_fields[] = {
    {"image", SIGN_OPTION_IMAGE, PATH},
    {"page_number", SIGN_OPTION_PAGE_NUMBER, NUMBER},
    {"x", SIGN_OPTION_X, NUMBER},
    {"y", SIGN_OPTION_Y, NUMBER},
    {"width", SIGN_OPTION_WIDTH, NUMBER},
    {"height", SIGN_OPTION_HEIGHT, NUMBER},
};

static bool token_valid(const char* text) {
	P11KitUri* uri = p11_kit_uri_new();
	if (!uri) {
		return false;
	}
	bool ok = g_str_has_prefix(text, "pkcs11:") &&
	          p11_kit_uri_parse(text, P11_KIT_URI_FOR_ANY, uri) ==
	              P11_KIT_URI_OK &&
	          !strchr(text, '?') && !p11_kit_uri_any_unrecognized(uri) &&
	          !p11_kit_uri_get_pin_value(uri) &&
	          !p11_kit_uri_get_pin_source(uri);
	p11_kit_uri_free(uri);
	return ok;
}

static const char* path_value(
    json_t*     object,
    const char* key,
    const char* base
) {
	char* absolute = g_canonicalize_filename(
	    json_string_value(json_object_get(object, key)),
	    base
	);
	json_t* value = json_string(absolute);
	g_free(absolute);
	if (!value || json_object_set_new(object, key, value) < 0) {
		return NULL;
	}
	return json_string_value(json_object_get(object, key));
}

static bool apply(
    sign_options* options,
    json_t*       object,
    const field*  f,
    const char*   base,
    const char*   path,
    GError**      error
) {
	json_t*     value = json_object_get(object, f->name);
	char        numeric[G_ASCII_DTOSTR_BUF_SIZE];
	const char* text = NULL;
	if (f->type == TEXT || f->type == PATH) {
		if (!json_is_string(value) || !json_string_length(value)) {
			return invalid(error, path, "expected a nonempty string");
		}
		text = json_string_value(value);
		if (f->option == SIGN_OPTION_TOKEN && !token_valid(text)) {
			return invalid(
			    error,
			    path,
			    "expected a PKCS#11 URI without PIN values or PIN sources"
			);
		}
		if (f->option == SIGN_OPTION_CERT_ID) {
			size_t len = strlen(text);
			if (len % 2) {
				return invalid(
				    error,
				    path,
				    "expected a hexadecimal certificate ID"
				);
			}
			for (size_t i = 0; i < len; ++i) {
				if (!g_ascii_isxdigit(text[i])) {
					return invalid(
					    error,
					    path,
					    "expected a hexadecimal certificate ID"
					);
				}
			}
		}
		if (f->type == PATH) {
			text = path_value(object, f->name, base);
		}
		if (!text) {
			return invalid(error, path, "cannot resolve path");
		}
	} else if (f->type == BOOLEAN) {
		if (!json_is_boolean(value)) {
			return invalid(error, path, "expected a boolean");
		}
		text = json_is_true(value) ? "true" : "false";
	} else {
		if (!json_is_number(value) || !isfinite(json_number_value(value))) {
			return invalid(error, path, "expected a finite number");
		}
		g_ascii_dtostr(numeric, sizeof(numeric), json_number_value(value));
		text = numeric;
	}
	char detail[512];
	if (!sign_options_set(options, f->option, text, detail, sizeof(detail))) {
		return invalid(error, path, "invalid signing option");
	}
	const char* choice = NULL;
	switch (f->option) {
	case SIGN_OPTION_ALGORITHM:
		choice = sign_option_choice(f->option, options->algo);
		break;
	case SIGN_OPTION_HASH:
		choice = sign_option_choice(f->option, options->hash);
		break;
	case SIGN_OPTION_LEVEL:
		choice = sign_option_choice(f->option, options->level);
		break;
	case SIGN_OPTION_FORMAT:
		choice = sign_option_choice(f->option, options->format);
		break;
	default:
		break;
	}
	if (choice && strcmp(choice, text)) {
		return invalid(error, path, "use the canonical signing option name");
	}
	return true;
}

static bool fields_read(
    sign_options* options,
    json_t*       object,
    const field*  fields,
    size_t        count,
    const char*   base,
    const char*   path,
    GError**      error
) {
	const char* key;
	json_t*     value;
	if (!json_is_object(object)) {
		return invalid(error, path, "expected an object");
	}
	json_object_foreach(object, key, value) {
		(void)value;
		const field* match = NULL;
		for (size_t i = 0; i < count; ++i) {
			if (!strcmp(fields[i].name, key)) {
				match = &fields[i];
				break;
			}
		}
		char* location = g_strdup_printf("%s.%s", path, key);
		bool  ok = match ? apply(options, object, match, base, location, error)
		                 : invalid(error, location, "unknown field");
		g_free(location);
		if (!ok) {
			return false;
		}
	}
	return true;
}

static bool list_read(
    sign_flow*    flow,
    sign_options* options,
    json_t*       object,
    const char*   key,
    const char*   base,
    const char*   path,
    GError**      error
) {
	json_t* array = json_object_get(object, key);
	size_t  count = json_array_size(array);
	bool    modules = !strcmp(key, "modules");
	if (!json_is_array(array) || count > (modules ? 1024u : 32u)) {
		return invalid(error, path, "expected a bounded array of strings");
	}
	const char** paths = modules ? g_new0(const char*, count + 1) : NULL;
	if (paths) {
		g_ptr_array_add(flow->allocations, paths);
		options->modules = (libssig_string_list){.items = paths,
		                                         .count = count};
	}
	for (size_t i = 0; i < count; ++i) {
		json_t* item = json_array_get(array, i);
		if (!json_is_string(item) || !json_string_length(item)) {
			return invalid(error, path, "expected nonempty string elements");
		}
		if (modules) {
			char* absolute = g_canonicalize_filename(
			    json_string_value(item),
			    base
			);
			json_t* replacement = json_string(absolute);
			g_free(absolute);
			if (!replacement || json_array_set_new(array, i, replacement) < 0) {
				return invalid(error, path, "cannot resolve module path");
			}
			paths[i] = json_string_value(json_array_get(array, i));
		} else {
			char detail[512];
			if (!sign_options_set(
			        options,
			        SIGN_OPTION_CERT_BUNDLE,
			        json_string_value(item),
			        detail,
			        sizeof(detail)
			    )) {
				return invalid(error, path, "unknown certificate bundle");
			}
		}
	}
	return true;
}

static bool appearance_read(
    sign_options* options,
    json_t*       appearance,
    const char*   base,
    const char*   path,
    GError**      error
) {
	if (!json_is_object(appearance) ||
	    !json_is_boolean(json_object_get(appearance, "enabled"))) {
		return invalid(
		    error,
		    path,
		    "appearance requires a boolean enabled field"
		);
	}
	bool enabled = json_is_true(json_object_get(appearance, "enabled"));
	/* Keep the validated JSON for disabled appearances, but do not execute it.
	 */
	json_t* settings = json_copy(appearance);
	json_object_del(settings, "enabled");
	bool ok = fields_read(
	    options,
	    settings,
	    appearance_fields,
	    G_N_ELEMENTS(appearance_fields),
	    base,
	    path,
	    error
	);
	if (ok && json_object_get(settings, "image")) {
		json_t* image = json_object_get(settings, "image");
		json_object_set(appearance, "image", image);
	}
	json_decref(settings);
	if (!ok) {
		return false;
	}
	if ((options->appearance_options &
	     (SIGN_APPEARANCE_WIDTH | SIGN_APPEARANCE_HEIGHT)) ==
	    (SIGN_APPEARANCE_WIDTH | SIGN_APPEARANCE_HEIGHT)) {
		return invalid(error, path, "specify width or height, not both");
	}
	if (enabled && (!options->appearance.page_number ||
	                (options->appearance_options &
	                 (SIGN_APPEARANCE_X | SIGN_APPEARANCE_Y)) !=
	                    (SIGN_APPEARANCE_X | SIGN_APPEARANCE_Y))) {
		return invalid(
		    error,
		    path,
		    "enabled appearances require page_number, x and y"
		);
	}
	if (!enabled) {
		options->appearance.page_number = 0;
		options->appearance_options = 0;
	}
	return true;
}

static bool read_document(
    sign_flow*  flow,
    size_t      index,
    const char* base,
    GError**    error
) {
	sign_options* options = &flow->documents[index];
	json_t*       document = json_array_get(
        json_object_get(flow->json, "documents"),
        index
    );
	char path[64];
	g_snprintf(path, sizeof(path), "documents[%zu]", index);
	if (!json_is_object(document)) {
		return invalid(error, path, "expected an object");
	}
	/* Dispatch nested groups separately; scalar fields share the options
	 * parser. */
	const char* key;
	json_t*     value;
	json_object_foreach(document, key, value) {
		char* location = g_strdup_printf("%s.%s", path, key);
		bool  ok;
		if (!strcmp(key, "tsa")) {
			ok = fields_read(
			    options,
			    value,
			    tsa_fields,
			    G_N_ELEMENTS(tsa_fields),
			    base,
			    location,
			    error
			);
		} else if (!strcmp(key, "appearance")) {
			ok = appearance_read(options, value, base, location, error);
		} else if (!strcmp(key, "modules") || !strcmp(key, "cert_bundles")) {
			ok = list_read(flow, options, document, key, base, location, error);
		} else {
			const field* match = NULL;
			for (size_t i = 0; i < G_N_ELEMENTS(document_fields); ++i) {
				if (!strcmp(document_fields[i].name, key)) {
					match = &document_fields[i];
					break;
				}
			}
			ok = match ? apply(options, document, match, base, location, error)
			           : invalid(error, location, "unknown field");
		}
		g_free(location);
		if (!ok) {
			return false;
		}
	}
	if (!options->input) {
		return invalid(error, path, "input is required");
	}
	if (!options->output) {
		char*   output = file_signed_path(options->input);
		json_t* value = output ? json_string(output) : NULL;
		free(output);
		if (!value || json_object_set_new(document, "output", value) < 0) {
			return invalid(error, path, "cannot allocate output path");
		}
		options->output = json_string_value(
		    json_object_get(document, "output")
		);
	}
	char detail[512];
	if (!sign_options_validate(options, detail, sizeof(detail))) {
		return invalid(error, path, "incompatible signing options");
	}
	return true;
}

sign_flow* sign_flow_parse_json(
    const char* text,
    size_t      len,
    const char* base_directory,
    GError**    error
) {
	if (!text || !len || len > SIGN_FLOW_MAX_JSON_SIZE ||
	    memchr(text, 0, len)) {
		invalid(error, "flow", "invalid JSON buffer or size");
		return NULL;
	}
	json_error_t parse_error;
	json_t* json = json_loadb(text, len, JSON_REJECT_DUPLICATES, &parse_error);
	if (!json) {
		g_set_error(
		    error,
		    SIGN_FLOW_ERROR,
		    SIGN_FLOW_ERROR_INVALID,
		    "Invalid JSON at line %d, column %d",
		    parse_error.line,
		    parse_error.column
		);
		return NULL;
	}
	sign_flow* flow = g_new0(sign_flow, 1);
	flow->json = json;
	flow->allocations = g_ptr_array_new_with_free_func(g_free);
	if (!json_is_object(json)) {
		invalid(error, "flow", "expected an object");
		goto failed;
	}
	const char* key;
	json_t*     value;
	json_object_foreach(json, key, value) {
		(void)value;
		if (strcmp(key, "version") && strcmp(key, "documents")) {
			invalid(error, key, "unknown flow field");
			goto failed;
		}
	}
	json_t* version = json_object_get(json, "version");
	if (!json_is_number(version) ||
	    json_number_value(version) != SIGN_FLOW_VERSION) {
		g_set_error_literal(
		    error,
		    SIGN_FLOW_ERROR,
		    SIGN_FLOW_ERROR_VERSION,
		    "Missing or unsupported flow version; expected version 1"
		);
		goto failed;
	}
	json_object_set_new(json, "version", json_integer(SIGN_FLOW_VERSION));
	json_t* documents = json_object_get(json, "documents");
	flow->count = json_array_size(documents);
	if (!json_is_array(documents) || !flow->count ||
	    flow->count > SIGN_FLOW_MAX_DOCUMENTS) {
		invalid(error, "documents", "expected between 1 and 1024 documents");
		goto failed;
	}
	flow->documents = g_new0(sign_options, flow->count);
	char* base = base_directory ? g_canonicalize_filename(base_directory, NULL)
	                            : g_get_current_dir();
	bool  ok = true;
	for (size_t i = 0; ok && i < flow->count; ++i) {
		ok = read_document(flow, i, base, error);
	}
	g_free(base);
	if (!ok) {
		goto failed;
	}
	GHashTable* inputs = g_hash_table_new_full(
	    (GHashFunc)g_file_hash,
	    (GEqualFunc)g_file_equal,
	    g_object_unref,
	    NULL
	);
	GHashTable* outputs = g_hash_table_new_full(
	    (GHashFunc)g_file_hash,
	    (GEqualFunc)g_file_equal,
	    g_object_unref,
	    NULL
	);
	for (size_t i = 0; i < flow->count; ++i) {
		g_hash_table_add(inputs, g_file_new_for_path(flow->documents[i].input));
	}
	for (size_t i = 0; ok && i < flow->count; ++i) {
		GFile* output = g_file_new_for_path(flow->documents[i].output);
		if (g_hash_table_contains(inputs, output) ||
		    g_hash_table_contains(outputs, output)) {
			invalid(
			    error,
			    "documents",
			    "output paths must be unique and must not replace any input"
			);
			ok = false;
		}
		g_hash_table_add(outputs, output);
	}
	g_hash_table_unref(inputs);
	g_hash_table_unref(outputs);
	if (!ok) {
		goto failed;
	}
	return flow;
failed:
	sign_flow_free(flow);
	return NULL;
}

size_t sign_flow_count(const sign_flow* flow) {
	return flow ? flow->count : 0;
}
const sign_options* sign_flow_document(const sign_flow* flow, size_t index) {
	return flow && index < flow->count ? &flow->documents[index] : NULL;
}
char* sign_flow_to_json(const sign_flow* flow, GError** error) {
	char* text = flow ? json_dumps(flow->json, JSON_INDENT(2) | JSON_SORT_KEYS)
	                  : NULL;
	if (!text) {
		invalid(error, "flow", "cannot serialize flow");
	}
	return text;
}
void sign_flow_free(sign_flow* flow) {
	if (!flow) {
		return;
	}
	json_decref(flow->json);
	g_ptr_array_unref(flow->allocations);
	g_free(flow->documents);
	g_free(flow);
}
bool sign_flow_run(
    const sign_flow*      flow,
    const sign_execution* execution,
    sign_flow_result*     result,
    GError**              error
) {
	sign_flow_result local;
	if (!result) {
		result = &local;
	}
	*result = (sign_flow_result){.failed_document = SIGN_FLOW_NO_DOCUMENT};
	if (!flow) {
		return invalid(error, "flow", "flow is required");
	}
	if (!g_atomic_int_compare_and_exchange(&running, 0, 1)) {
		g_set_error_literal(
		    error,
		    SIGN_FLOW_ERROR,
		    SIGN_FLOW_ERROR_BUSY,
		    "Another flow is already running"
		);
		return false;
	}
	bool ok = true;
	for (size_t i = 0; ok && i < flow->count; ++i) {
		ok = sign_document_execute(
		    &flow->documents[i],
		    execution,
		    &result->document,
		    error
		);
		if (ok) {
			++result->completed;
		} else {
			result->failed_document = i;
			g_prefix_error(error, "documents[%zu]: ", i);
		}
	}
	g_atomic_int_set(&running, 0);
	return ok;
}
