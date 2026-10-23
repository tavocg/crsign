/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Some standalone argp headers redefine compiler macros in strict C mode.
 * Keep those definitions from changing subsequent libc/GLib inline functions.
 */
#pragma push_macro("__attribute__")
#pragma push_macro("__format__")
#pragma push_macro("__printf__")
#include <argp.h>
#pragma pop_macro("__printf__")
#pragma pop_macro("__format__")
#pragma pop_macro("__attribute__")
#include <errno.h>
#include <glib.h>
#include <limits.h>
#include <math.h>
#include <sign/arg.h>
#include <sign/certs/certs.h>
#include <sign/config.h>
#include <stdarg.h>
#include <string.h>
#ifdef G_OS_WIN32
#include <glib/gwin32.h>
#endif

const char* argp_program_version = "Sign " SIGN_VERSION;
typedef struct {
	int         key;
	const char* value;
} cli_setting;

typedef struct {
	sign_args   args;
	const char* profile;
	GArray*     settings;
	GPtrArray*  modules;
} parse_context;

static const struct argp_option options[] = {
    {"no-bundled-certs",
     NO_BUNDLED_CERTS,
     0,
     0,
     "Disable embedded intermediates and fallback roots",
     0},
    {"cert-bundle",
     CERT_BUNDLE,
     "NAME",
     0,
     "Select an embedded bundle (repeatable; default: all)",
     0},
    {"content",
     CONTENT,
     0,
     0,
     "Show certificate DER in hexadecimal (tokens only)",
     0},
    {"gui", 'g', 0, 0, "Open the graphical interface", 0},
    {"verify", 'v', 0, OPTION_HIDDEN, "Alias for verify", 0},
    {"json", JSON, 0, 0, "Print operation results as JSON", 0},
    {"all", ALL_CERTIFICATES, 0, 0, "List all certificates (tokens only)", 0},
    {"output",
     'o',
     "FILE",
     0,
     "Output (default: basename-signed.ext; never overwrite)",
     0},
    {"profile",
     'p',
     "LABEL",
     0,
     "Use a profile from crsign.toml (signing or tokens)",
     0},
    {"module",
     'm',
     "PATH",
     0,
     "Load a PKCS#11 module (repeatable; replaces configured modules)",
     0},
    {"token", 't', "URI", 0, "Exact PKCS#11 token URI", 0},
    {"key", 'k', "ID_HEX", 0, "Certificate ID in hexadecimal", 0},
    {"algorithm", 'a', "ALGORITHM", 0, "rsa-pkcs (default), rsa-pss, ecdsa", 0},
    {"level", 'l', "LEVEL", 0, "B-LTA (default), B-LT, B-T, B-B", 0},
    {"format",
     'f',
     "FORMAT",
     0,
     "cades, pades, xades or jades (default: pades for PDF)",
     0},
    {"hash", 'H', "HASH", 0, "sha256 (default), sha384, sha512", 0},
    {"authority",
     'A',
     "URL",
     0,
     "RFC 3161 TSA (HTTP/HTTPS; automatic if omitted)",
     0},
    {
        "ca-file",
        CA_FILE,
        "FILE",
        0,
        "Additional document trust roots, PEM/DER",
        0,
    },
    {
        "intermediates",
        INTERMEDIATES,
        "FILE",
        0,
        "Document intermediates, PEM/DER",
        0,
    },
    {
        "tsa-ca-file",
        TSA_CA_FILE,
        "FILE",
        0,
        "Additional TSA trust roots, PEM/DER",
        0,
    },
    {
        "tsa-intermediates",
        TSA_INTERMEDIATES,
        "FILE",
        0,
        "TSA intermediates, PEM/DER",
        0,
    },
    {"detached", DETACHED, 0, 0, "Sign with external content", 0},
    {
        "mime-type",
        MIME_TYPE,
        "MIME",
        0,
        "Content MIME type (default: libmagic)",
        0,
    },
    {"page-number",
     'n',
     "PAGE",
     0,
     "Visible PDF signature on page PAGE (starts at 1; requires -x and -y)",
     0},
    {"x",
     'x',
     "POINTS",
     0,
     "Image left edge from the visible page's upper-left corner",
     0},
    {"y",
     'y',
     "POINTS",
     0,
     "Image top edge from the visible page's upper-left corner",
     0},
    {"width",
     'w',
     "POINTS",
     0,
     "Image width (default: 144; 12–720 points; preserves aspect ratio)",
     0},
    {"height",
     'h',
     "POINTS",
     0,
     "Image height instead of width (12–720 points; preserves aspect ratio)",
     0},
    {"signature-image",
     'i',
     "FILE",
     0,
     "PDF appearance image (default: generated SVG with signer and date)",
     0},
    {"help", HELP, 0, 0, "Show this help message (also -?)", 0},
    {0}
};

bool arg_setting(
    sign_args*  args,
    int         key,
    const char* value,
    char*       error,
    size_t      size
) {
	sign_option option;
	switch (key) {
	case 'o':
		option = SIGN_OPTION_OUTPUT;
		break;
	case 't':
		option = SIGN_OPTION_TOKEN;
		break;
	case 'k':
		option = SIGN_OPTION_CERT_ID;
		break;
	case 'a':
		option = SIGN_OPTION_ALGORITHM;
		break;
	case 'H':
		option = SIGN_OPTION_HASH;
		break;
	case 'l':
		option = SIGN_OPTION_LEVEL;
		break;
	case 'f':
		option = SIGN_OPTION_FORMAT;
		break;
	case 'A':
		option = SIGN_OPTION_AUTHORITY;
		break;
	case 'n':
		option = SIGN_OPTION_PAGE_NUMBER;
		break;
	case 'x':
		option = SIGN_OPTION_X;
		break;
	case 'y':
		option = SIGN_OPTION_Y;
		break;
	case 'w':
		option = SIGN_OPTION_WIDTH;
		break;
	case 'h':
		option = SIGN_OPTION_HEIGHT;
		break;
	case 'i':
		option = SIGN_OPTION_IMAGE;
		break;
	case CA_FILE:
		option = SIGN_OPTION_CA_FILE;
		break;
	case INTERMEDIATES:
		option = SIGN_OPTION_INTERMEDIATES;
		break;
	case TSA_CA_FILE:
		option = SIGN_OPTION_TSA_CA_FILE;
		break;
	case TSA_INTERMEDIATES:
		option = SIGN_OPTION_TSA_INTERMEDIATES;
		break;
	case NO_BUNDLED_CERTS:
		option = SIGN_OPTION_NO_BUNDLED_CERTS;
		break;
	case CERT_BUNDLE:
		option = SIGN_OPTION_CERT_BUNDLE;
		break;
	case DETACHED:
		option = SIGN_OPTION_DETACHED;
		break;
	case MIME_TYPE:
		option = SIGN_OPTION_MIME_TYPE;
		break;
	default:
		snprintf(error, size, "Unknown signing option");
		return false;
	}
	if (!sign_options_set(&args->options, option, value, error, size)) {
		return false;
	}
	if (key != CA_FILE && key != INTERMEDIATES && key != TSA_CA_FILE &&
	    key != TSA_INTERMEDIATES && key != NO_BUNDLED_CERTS &&
	    key != CERT_BUNDLE) {
		args->signing_options = true;
	}
	return true;
}

static void args_validate(struct argp_state* state, const sign_args* args) {
	bool verify = args->command == SIGN_COMMAND_VERIFY;
	bool tokens = args->command == SIGN_COMMAND_TOKENS;
	char detail[512];
	if (!sign_options_validate(&args->options, detail, sizeof(detail))) {
		argp_error(state, "%s", detail);
	}
	if (args->gui) {
		if (state->argc > 2) {
			argp_error(state, "--gui cannot be combined with CLI operations");
		}
		return;
	}

	if (!args->options.input && !tokens) {
		if (state->argc > 1) {
			argp_error(state, "A file is required");
		}
		return;
	}

	if ((verify || tokens) && args->signing_options) {
		argp_error(
		    state,
		    "Signing options require the default signing command"
		);
	}

	if (tokens &&
	    (args->options.input || args->options.ca_file ||
	     args->options.intermediates || args->options.tsa_ca_file ||
	     args->options.tsa_intermediates || args->options.no_bundled_certs ||
	     args->options.cert_bundle_count)) {
		argp_error(state, "tokens accepts no file or trust options");
	}

	if (!tokens && (args->all_certificates || args->content)) {
		argp_error(state, "--all and --content require tokens");
	}
}

static void positional_parse(
    struct argp_state* state,
    sign_args*         args,
    char*              arg
) {
	if (state->arg_num == 0 && (!strcmp(arg, "verify") || !strcmp(arg, "v"))) {
		if (args->command != SIGN_COMMAND_SIGN) {
			argp_error(state, "Conflicting commands");
		}
		args->command = SIGN_COMMAND_VERIFY;
		return;
	}

	if (state->arg_num == 0 && (!strcmp(arg, "tokens") || !strcmp(arg, "t"))) {
		if (args->command != SIGN_COMMAND_SIGN) {
			argp_error(state, "Conflicting commands");
		}
		args->command = SIGN_COMMAND_TOKENS;
		return;
	}

	if (args->options.input) {
		argp_error(state, "Only one input file is supported");
	}
	args->options.input = arg;
}

static error_t parse_option(int key, char* arg, struct argp_state* state) {
	parse_context* context = state->input;
	sign_args*     args = &context->args;
	char           error[512];
	switch (key) {
	case 'g':
		args->gui = true;
		break;
	case 'v':
		if (args->command == SIGN_COMMAND_TOKENS) {
			argp_error(state, "Conflicting commands");
		}
		args->command = SIGN_COMMAND_VERIFY;
		break;
	case 'p':
		context->profile = arg;
		break;
	case 'm': {
		if (!arg[0]) {
			argp_error(state, "--module requires a nonempty path");
		}
		/* CLI paths are relative to the working directory, unlike TOML paths.
		 */
		char* path = g_canonicalize_filename(arg, NULL);
		bool  duplicate = false;
		for (guint i = 0; i < context->modules->len; ++i) {
			if (!strcmp(path, g_ptr_array_index(context->modules, i))) {
				duplicate = true;
				break;
			}
		}
		if (duplicate) {
			g_free(path);
		} else {
			g_ptr_array_add(context->modules, path);
		}
		break;
	}
	case 'o':
	case 'n':
	case 'x':
	case 'y':
	case 'w':
	case 'h':
	case 'i':
	case 't':
	case 'k':
	case 'a':
	case 'H':
	case 'l':
	case 'f':
	case 'A':
	case CA_FILE:
	case INTERMEDIATES:
	case TSA_CA_FILE:
	case TSA_INTERMEDIATES:
	case NO_BUNDLED_CERTS:
	case CERT_BUNDLE:
	case DETACHED:
	case MIME_TYPE: {
		cli_setting setting = {key, arg ? arg : "true"};
		if (!arg_setting(args, key, setting.value, error, sizeof(error))) {
			argp_error(state, "%s", error);
		}
		g_array_append_val(context->settings, setting);
		break;
	}
	case HELP:
		argp_state_help(state, stdout, ARGP_HELP_STD_HELP);
		break;
	case JSON:
		args->json = true;
		break;
	case CONTENT:
		args->content = true;
		break;
	case ALL_CERTIFICATES:
		args->all_certificates = true;
		break;
	case ARGP_KEY_ARG:
		positional_parse(state, args, arg);
		break;
	case ARGP_KEY_END:
		/* Check command compatibility before opening a configuration file. */
		if (context->profile &&
		    (args->command == SIGN_COMMAND_VERIFY || args->gui)) {
			argp_error(state, "--profile requires signing or tokens");
		}
		if (context->modules->len && args->command == SIGN_COMMAND_VERIFY) {
			argp_error(state, "--module requires signing or tokens");
		}
		args_validate(state, args);
		if ((args->command == SIGN_COMMAND_SIGN && args->options.input) ||
		    args->command == SIGN_COMMAND_TOKENS) {
			bool cli_bundles = args->options.cert_bundle_count ||
			                   args->options.no_bundled_certs;
			sign_args resolved = {0};
			if (!config_load(
			        &resolved,
			        context->profile,
			        error,
			        sizeof(error)
			    )) {
				argp_error(state, "%s", error);
			}
			if (args->command == SIGN_COMMAND_TOKENS) {
				/* Listing uses module selection without applying signing
				 * options. */
				resolved = (sign_args){
				    .options = {.modules = resolved.options.modules},
				    .config = resolved.config
				};
			}
			if (context->modules->len) {
				resolved.options.modules = (libssig_string_list){
				    .items = (const char**)context->modules->pdata,
				    .count = context->modules->len
				};
			}
			/* Command/UI options do not belong to the signing profile. */
			resolved.command = args->command;
			resolved.options.input = args->options.input;
			resolved.gui = args->gui;
			resolved.json = args->json;
			resolved.all_certificates = args->all_certificates;
			resolved.content = args->content;
			resolved.signing_options = args->signing_options;
			*args = resolved;
			/* A CLI bundle selection replaces the profile's entire selection.
			 */
			if (cli_bundles) {
				args->options.cert_bundle_count = 0;
				args->options.no_bundled_certs = false;
			}
			for (guint i = 0; i < context->settings->len; ++i) {
				cli_setting setting = g_array_index(
				    context->settings,
				    cli_setting,
				    i
				);
				if (!arg_setting(
				        args,
				        setting.key,
				        setting.value,
				        error,
				        sizeof(error)
				    )) {
					argp_error(state, "%s", error);
				}
			}
			args_validate(state, args);
		}
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static const struct argp parser = {
    .options = options,
    .parser = parse_option,
    .args_doc = "FILE [-o OUTPUT]\nverify FILE (alias: v)\ntokens (alias: t)",
    .doc = "Sign: AdES signing and verification. System trust is used by "
           "default."
};

sign_args arg_parse(int argc, char** argv) {
	/* main() uses the system code page on Windows; GLib file APIs use UTF-8. */
#ifdef G_OS_WIN32
	char** owned_argv = g_win32_get_command_line();
	argc = (int)g_strv_length(owned_argv);
#else
	char** owned_argv = g_strdupv(argv);
#endif
	(void)argv;
	parse_context context = {
	    .settings = g_array_new(false, false, sizeof(cli_setting)),
	    .modules = g_ptr_array_new_with_free_func(g_free)
	};
	argp_parse(&parser, argc, owned_argv, 0, 0, &context);
	if (context.modules->len) {
		g_ptr_array_add(context.modules, NULL);
		context.args.cli_modules = (char**)
		    g_ptr_array_free(context.modules, false);
		context.args.options.modules.items = (const char**)
		                                         context.args.cli_modules;
	} else {
		g_ptr_array_free(context.modules, true);
	}
	context.args.argv = owned_argv;
	g_array_free(context.settings, true);
	return context.args;
}

void arg_help(char* program_name) {
	argp_help(&parser, stdout, ARGP_HELP_STD_HELP, program_name);
}

void arg_free(sign_args* args) {
	config_free(args->config);
	g_strfreev(args->cli_modules);
	g_strfreev(args->argv);
	*args = (sign_args){0};
}
