/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <jansson.h>
#include <math.h>
#include <p11-kit/uri.h>
#include <sign/flow.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

struct sign_flow {
	json_t*         json;
	sign_flow_item* documents;
	size_t          count;
	GPtrArray*      allocations;
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
    {"original", SIGN_OPTION_ORIGINAL, PATH},
    {"extend", SIGN_OPTION_EXTEND, BOOLEAN},
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
    {"information", SIGN_OPTION_APPEARANCE_INFORMATION, BOOLEAN},
    {"page_number", SIGN_OPTION_PAGE_NUMBER, NUMBER},
    {"x", SIGN_OPTION_X, NUMBER},
    {"y", SIGN_OPTION_Y, NUMBER},
    {"width", SIGN_OPTION_WIDTH, NUMBER},
    {"height", SIGN_OPTION_HEIGHT, NUMBER},
};

/* The builder is deliberately round-tripped through the public JSON parser:
 * GUI and API clients have the same path/collision/schema guarantees. */
static void json_text_set(json_t* object, const char* key, const char* text) {
	if (text && *text) {
		json_t* value = json_string(text);
		json_object_set_new(object, key, value ? value : json_null());
	}
}
sign_flow* sign_flow_from_items(
    const sign_flow_item* items,
    size_t                count,
    GError**              error
) {
	if (!items || !count || count > SIGN_FLOW_MAX_DOCUMENTS) {
		invalid(error, "documents", "expected between 1 and 1024 documents");
		return NULL;
	}
	json_t* documents = json_array();
	for (size_t n = 0; n < count; ++n) {
		const sign_flow_item* item = &items[n];
		const sign_options*   o = &item->options;
		char                  detail[512];
		if (o->cert_bundle_count > G_N_ELEMENTS(o->cert_bundles) ||
		    o->modules.count > 1024 ||
		    (o->modules.count && !o->modules.items)) {
			json_decref(documents);
			invalid(error, "documents", "invalid option list");
			return NULL;
		}
		for (size_t b = 0; b < o->cert_bundle_count; ++b) {
			if (!o->cert_bundles[b] ||
			    !g_utf8_validate(o->cert_bundles[b], -1, NULL)) {
				json_decref(documents);
				invalid(error, "cert_bundles", "invalid certificate bundle");
				return NULL;
			}
		}
		for (size_t m = 0; m < o->modules.count; ++m) {
			if (!o->modules.items[m] ||
			    !g_utf8_validate(o->modules.items[m], -1, NULL)) {
				json_decref(documents);
				invalid(error, "modules", "invalid module path");
				return NULL;
			}
		}
		if (item->operation == SIGN_FLOW_SIGN &&
		    !sign_options_validate(o, detail, sizeof(detail))) {
			json_decref(documents);
			invalid(error, "documents", detail);
			return NULL;
		}
		json_t* doc = json_object();
		json_array_append_new(documents, doc);
		json_object_set_new(
		    doc,
		    "operation",
		    json_string(item->operation == SIGN_FLOW_VERIFY ? "verify" : "sign")
		);
		json_text_set(doc, "input", o->input);
		json_text_set(doc, "ca", o->ca_file);
		json_text_set(doc, "intermediates", o->intermediates);
		json_object_set_new(
		    doc,
		    "no_bundled_certs",
		    json_boolean(o->no_bundled_certs)
		);
		json_t* bundles = json_array();
		for (size_t b = 0; b < o->cert_bundle_count; ++b) {
			json_array_append_new(bundles, json_string(o->cert_bundles[b]));
		}
		json_object_set_new(doc, "cert_bundles", bundles);
		json_t* tsa = json_object();
		json_text_set(tsa, "ca", o->tsa_ca_file);
		json_text_set(tsa, "intermediates", o->tsa_intermediates);
		json_object_set_new(doc, "tsa", tsa);
		if (item->operation == SIGN_FLOW_VERIFY) {
			json_text_set(doc, "content", item->content);
			continue;
		}
		if (item->operation != SIGN_FLOW_SIGN) {
			json_decref(documents);
			invalid(error, "operation", "unsupported operation");
			return NULL;
		}
		json_text_set(doc, "output", o->output);
		json_text_set(doc, "original", o->original);
		json_object_set_new(doc, "extend", json_boolean(o->extend));
		json_text_set(doc, "token", o->uri);
		json_text_set(doc, "cert_id", o->id);
		json_text_set(doc, "mime_type", o->mime_type);
		json_text_set(
		    doc,
		    "algorithm",
		    sign_option_choice(SIGN_OPTION_ALGORITHM, o->algo)
		);
		json_text_set(
		    doc,
		    "hash",
		    sign_option_choice(SIGN_OPTION_HASH, o->hash)
		);
		json_text_set(
		    doc,
		    "level",
		    sign_option_choice(SIGN_OPTION_LEVEL, o->level)
		);
		if (o->format_explicit) {
			json_text_set(
			    doc,
			    "format",
			    sign_option_choice(SIGN_OPTION_FORMAT, o->format)
			);
		}
		json_object_set_new(doc, "detached", json_boolean(o->detached));
		json_text_set(tsa, "url", o->authority);
		json_t* modules = json_array();
		for (size_t m = 0; m < o->modules.count; ++m) {
			json_array_append_new(modules, json_string(o->modules.items[m]));
		}
		json_object_set_new(doc, "modules", modules);
		json_t* appearance = json_pack(
		    "{s:b}",
		    "enabled",
		    o->appearance.page_number != 0
		);
		json_object_set_new(doc, "appearance", appearance);
		if (o->appearance.page_number) {
			json_object_set_new(
			    appearance,
			    "page_number",
			    json_integer(o->appearance.page_number)
			);
			json_object_set_new(appearance, "x", json_real(o->appearance.x));
			json_object_set_new(appearance, "y", json_real(o->appearance.y));
			if (o->appearance.width > 0) {
				json_object_set_new(
				    appearance,
				    "width",
				    json_real(o->appearance.width)
				);
			} else if (o->appearance.height > 0) {
				json_object_set_new(
				    appearance,
				    "height",
				    json_real(o->appearance.height)
				);
			}
			json_text_set(appearance, "image", o->appearance_file);
			json_object_set_new(
			    appearance,
			    "information",
			    json_boolean(o->appearance_information)
			);
		}
	}
	json_t* root = json_pack(
	    "{s:i,s:o}",
	    "version",
	    SIGN_FLOW_VERSION,
	    "documents",
	    documents
	);
	char* text = json_dumps(root, JSON_COMPACT);
	json_decref(root);
	if (!text) {
		invalid(error, "flow", "cannot encode options as JSON");
		return NULL;
	}
	sign_flow* flow = sign_flow_parse_json(text, strlen(text), NULL, error);
	if (flow) {
		/* Overwrite is an execution-time confirmation, not flow data. */
		for (size_t n = 0; n < flow->count; ++n) {
			flow->documents[n].options.overwrite = items[n].options.overwrite;
		}
	}
	free(text);
	return flow;
}

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
	sign_flow_item* item = &flow->documents[index];
	sign_options*   options = &item->options;
	json_t*         document = json_array_get(
        json_object_get(flow->json, "documents"),
        index
    );
	char path[64];
	g_snprintf(path, sizeof(path), "documents[%zu]", index);
	if (!json_is_object(document)) {
		return invalid(error, path, "expected an object");
	}
	json_t* operation = json_object_get(document, "operation");
	if (operation) {
		if (!json_is_string(operation) ||
		    (strcmp(json_string_value(operation), "sign") &&
		     strcmp(json_string_value(operation), "verify"))) {
			return invalid(error, path, "operation must be sign or verify");
		}
		item->operation = !strcmp(json_string_value(operation), "verify")
		                      ? SIGN_FLOW_VERIFY
		                      : SIGN_FLOW_SIGN;
	}
	/* Dispatch nested groups separately; scalar fields share the options
	 * parser. */
	const char* key;
	json_t*     value;
	json_object_foreach(document, key, value) {
		char* location = g_strdup_printf("%s.%s", path, key);
		bool  ok;
		if (!strcmp(key, "operation")) {
			g_free(location);
			continue;
		}
		if (item->operation == SIGN_FLOW_VERIFY && strcmp(key, "input") &&
		    strcmp(key, "content") && strcmp(key, "ca") &&
		    strcmp(key, "intermediates") && strcmp(key, "no_bundled_certs") &&
		    strcmp(key, "cert_bundles") && strcmp(key, "tsa")) {
			g_free(location);
			return invalid(
			    error,
			    path,
			    "field is not allowed for verification"
			);
		}
		if (!strcmp(key, "content") && item->operation == SIGN_FLOW_VERIFY) {
			ok = json_is_string(value) && json_string_length(value);
			if (ok) {
				item->content = path_value(document, key, base);
				ok = item->content != NULL;
			}
			if (!ok) {
				invalid(error, location, "expected a nonempty content path");
			}
		} else if (!strcmp(key, "tsa")) {
			ok = fields_read(
			    options,
			    value,
			    item->operation == SIGN_FLOW_VERIFY ? tsa_fields + 1
			                                        : tsa_fields,
			    G_N_ELEMENTS(tsa_fields) -
			        (item->operation == SIGN_FLOW_VERIFY),
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
	if (item->operation == SIGN_FLOW_SIGN && !options->output) {
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
	flow->documents = g_new0(sign_flow_item, flow->count);
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
	/* Protect original inputs; a later operation may consume a distinct
	 * output produced earlier in this flow (for example, to co-sign it). */
	for (size_t i = 0; i < flow->count; ++i) {
		const sign_flow_item* item = &flow->documents[i];
		if (item->operation == SIGN_FLOW_SIGN) {
			const char* paths[] = {item->options.input, item->options.original};
			for (size_t j = 0; j < G_N_ELEMENTS(paths); ++j) {
				if (!paths[j]) {
					continue;
				}
				GFile* file = g_file_new_for_path(paths[j]);
				if (!g_hash_table_contains(outputs, file)) {
					g_hash_table_add(inputs, file);
				} else {
					g_object_unref(file);
				}
			}
			g_hash_table_add(
			    outputs,
			    g_file_new_for_path(item->options.output)
			);
		}
	}
	g_hash_table_remove_all(outputs);
	for (size_t i = 0; ok && i < flow->count; ++i) {
		const sign_flow_item* item = &flow->documents[i];
		if (item->operation == SIGN_FLOW_VERIFY) {
			g_hash_table_add(inputs, g_file_new_for_path(item->options.input));
			if (item->content) {
				g_hash_table_add(inputs, g_file_new_for_path(item->content));
			}
			continue;
		}
		GFile* output = g_file_new_for_path(item->options.output);
		if (g_hash_table_contains(inputs, output) ||
		    g_hash_table_contains(outputs, output)) {
			invalid(
			    error,
			    "documents",
			    "output paths must be unique and must not replace an input"
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
	const sign_flow_item* item = sign_flow_get_item(flow, index);
	return item ? &item->options : NULL;
}
const sign_flow_item* sign_flow_get_item(const sign_flow* flow, size_t index) {
	return flow && index < flow->count ? &flow->documents[index] : NULL;
}
void sign_flow_result_clear(sign_flow_result* result) {
	if (!result) {
		return;
	}
	for (size_t i = 0; i < result->count; ++i) {
		libssig_verify_free(&result->documents[i].verification);
	}
	g_free(result->documents);
	*result = (sign_flow_result){.failed_document = SIGN_FLOW_NO_DOCUMENT};
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
	result->count = flow->count;
	result->documents = g_new0(sign_flow_document_result, flow->count);
	for (size_t i = 0; i < flow->count; ++i) {
		result->documents[i].operation = flow->documents[i].operation;
	}
	bool ok = true;
	for (size_t i = 0; ok && i < flow->count; ++i) {
		const sign_flow_item*      item = &flow->documents[i];
		sign_flow_document_result* report = &result->documents[i];
		report->attempted = true;
		if (execution && execution->cancellable &&
		    g_cancellable_set_error_if_cancelled(
		        execution->cancellable,
		        error
		    )) {
			ok = false;
		} else if (item->operation == SIGN_FLOW_VERIFY) {
			ok = sign_document_verify(
			    &item->options,
			    item->content,
			    &report->verification_status,
			    &report->verification,
			    error
			);
		} else {
			ok = sign_document_execute(
			    &item->options,
			    execution,
			    &report->signing,
			    error
			);
			result->document = report->signing;
		}
		report->ok = ok;
		if (ok) {
			++result->completed;
		} else {
			result->failed_document = i;
			g_prefix_error(error, "documents[%zu]: ", i);
		}
	}
	g_atomic_int_set(&running, 0);
	if (result == &local) {
		sign_flow_result_clear(result);
	}
	return ok;
}
