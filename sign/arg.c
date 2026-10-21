/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <argp.h>
#include <sign/arg.h>
#include <string.h>
#include <strings.h>

const char* argp_program_version = "Sign " SIGN_VERSION;
enum {
	CA_FILE = 256,
	INTERMEDIATES,
	TSA_CA_FILE,
	TSA_INTERMEDIATES,
	DETACHED,
	MIME_TYPE,
	JSON,
	ALL_CERTIFICATES,
	CONTENT
};
static const struct argp_option options[] = {
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
    {"authority", 'A', "URL", 0, "RFC 3161 TSA (HTTP/HTTPS)", 0},
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
    {"help", 'h', 0, 0, "Show this help message", 0},
    {0}
};

static unsigned int choice_parse(
    struct argp_state* state,
    const char*        arg,
    const char* const* choices,
    unsigned int       count
) {
	for (unsigned int i = 0; i < count; ++i) {
		if (!strcasecmp(arg, choices[i])) {
			return i;
		}
	}
	argp_error(state, "Unknown value: %s", arg);
	return 0;
}

static void args_validate(struct argp_state* state, const sign_args* args) {
	bool verify = args->command == SIGN_COMMAND_VERIFY;
	bool tokens = args->command == SIGN_COMMAND_TOKENS;
	if (args->gui) {
		if (state->argc > 2) {
			argp_error(state, "--gui cannot be combined with CLI operations");
		}
		return;
	}

	if (!args->input && !tokens) {
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

	if (tokens && (args->input || args->ca_file || args->intermediates ||
	               args->tsa_ca_file || args->tsa_intermediates)) {
		argp_error(state, "tokens accepts no file or trust options");
	}

	if (!tokens && (args->all_certificates || args->content)) {
		argp_error(state, "--all and --content require tokens");
	}

	if (!verify && !tokens && args->level != LIBSSIG_LEVEL_B_B &&
	    !args->authority) {
		argp_error(
		    state,
		    "The selected level requires -A TSA_URL; use -l B-B for an offline "
		    "signature"
		);
	}
	if (!verify && !tokens && args->format == LIBSSIG_FORMAT_PADES &&
	    args->detached) {
		argp_error(state, "PAdES cannot use --detached");
	}
}

static void positional_parse(
    struct argp_state* state,
    sign_args*         args,
    char*              arg
) {
	if (state->arg_num == 0 && !strcmp(arg, "verify")) {
		if (args->command != SIGN_COMMAND_SIGN) {
			argp_error(state, "Conflicting commands");
		}
		args->command = SIGN_COMMAND_VERIFY;
		return;
	}

	if (state->arg_num == 0 && !strcmp(arg, "tokens")) {
		if (args->command != SIGN_COMMAND_SIGN) {
			argp_error(state, "Conflicting commands");
		}
		args->command = SIGN_COMMAND_TOKENS;
		return;
	}

	if (args->input) {
		argp_error(state, "Only one input file is supported");
	}
	args->input = arg;
}

static error_t parse_option(int key, char* arg, struct argp_state* state) {
	sign_args*        args = state->input;
	const char* const algorithms[] = {"rsa-pkcs", "rsa-pss", "ecdsa"};
	const char* const hashes[] = {"sha256", "sha384", "sha512"};
	const char* const levels[] = {"B-LTA", "B-B", "B-T", "B-LT"};
	const char* const formats[] = {"cades", "cms", "pades", "xades", "jades"};
	if (key == 'o' || key == 't' || key == 'k' || key == 'a' || key == 'H' ||
	    key == 'A' || key == 'l' || key == 'f' || key == DETACHED ||
	    key == MIME_TYPE) {
		args->signing_options = true;
	}
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
	case 'o':
		args->output = arg;
		break;
	case 't':
		args->uri = arg;
		break;
	case 'k':
		args->id = arg;
		break;
	case 'a':
		args->algo = (libssig_algo)choice_parse(
		    state,
		    !strcasecmp(arg, "rsa") ? "rsa-pkcs" : arg,
		    algorithms,
		    3
		);
		break;
	case 'H':
		args->hash = (libssig_hash)choice_parse(state, arg, hashes, 3);
		break;
	case 'l':
		args->level = (libssig_level)choice_parse(state, arg, levels, 4);
		break;
	case 'f':
		args->format = (libssig_format)choice_parse(state, arg, formats, 5);
		if (args->format == LIBSSIG_FORMAT_CMS) {
			argp_error(state, "Use cades, pades, xades or jades");
		}
		args->format_explicit = true;
		break;
	case 'A':
		args->authority = arg;
		break;
	case CA_FILE:
		args->ca_file = arg;
		break;
	case INTERMEDIATES:
		args->intermediates = arg;
		break;
	case TSA_CA_FILE:
		args->tsa_ca_file = arg;
		break;
	case TSA_INTERMEDIATES:
		args->tsa_intermediates = arg;
		break;
	case DETACHED:
		args->detached = true;
		break;
	case MIME_TYPE:
		args->mime_type = arg;
		break;
	case 'h':
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
		args_validate(state, args);
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static const struct argp parser = {
    .options = options,
    .parser = parse_option,
    .args_doc = "FILE [-o OUTPUT]\nverify FILE\ntokens",
    .doc = "Sign: CAdES signing and verification. System trust is used by "
           "default."
};

sign_args arg_parse(int argc, char** argv) {
	sign_args args = {0};
	argp_parse(&parser, argc, argv, 0, 0, &args);
	return args;
}

void arg_help(char* program_name) {
	argp_help(&parser, stdout, ARGP_HELP_STD_HELP, program_name);
}
