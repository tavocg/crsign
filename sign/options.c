/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <errno.h>
#include <glib.h>
#include <limits.h>
#include <math.h>
#include <sign/certs/certs.h>
#include <sign/options.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static bool setting_error(char* error, size_t size, const char* format, ...) {
	va_list ap;
	va_start(ap, format);
	vsnprintf(error, size, format, ap);
	va_end(ap);
	return false;
}

static int choice_parse(
    const char*        value,
    const char* const* choices,
    int                count
) {
	for (int i = 0; i < count; ++i) {
		if (!g_ascii_strcasecmp(value, choices[i])) {
			return i;
		}
	}
	return -1;
}

static const char* const algorithms[] = {"rsa-pkcs", "rsa-pss", "ecdsa"};
static const char* const hashes[] = {"sha256", "sha384", "sha512"};
static const char* const levels[] = {"B-LTA", "B-B", "B-T", "B-LT"};
static const char* const formats[] =
    {"cades", "cms", "pades", "xades", "jades"};

const char* sign_option_choice(sign_option key, unsigned int index) {
	const char* const* items = NULL;
	size_t             count = 0;
	switch (key) {
	case SIGN_OPTION_ALGORITHM:
		items = algorithms;
		count = G_N_ELEMENTS(algorithms);
		break;
	case SIGN_OPTION_HASH:
		items = hashes;
		count = G_N_ELEMENTS(hashes);
		break;
	case SIGN_OPTION_LEVEL:
		items = levels;
		count = G_N_ELEMENTS(levels);
		break;
	case SIGN_OPTION_FORMAT:
		items = formats;
		count = G_N_ELEMENTS(formats);
		break;
	default:
		return NULL;
	}
	return index < count ? items[index] : NULL;
}

bool sign_options_set(
    sign_options* args,
    sign_option   key,
    const char*   value,
    char*         error,
    size_t        error_size
) {
	int choice = 0;
	switch (key) {
	case SIGN_OPTION_PAGE_NUMBER: {
		char* end = NULL;
		errno = 0;
		guint64 page = g_ascii_strtoull(value, &end, 10);
		if (!value[0] || g_ascii_isspace(value[0]) || value[0] == '-' ||
		    value[0] == '+' || errno || *end || !page || page > UINT_MAX) {
			return setting_error(
			    error,
			    error_size,
			    "--page-number requires a positive page number starting at 1"
			);
		}
		args->appearance.page_number = (unsigned int)page;
		break;
	}
	case SIGN_OPTION_X:
	case SIGN_OPTION_Y:
	case SIGN_OPTION_WIDTH:
	case SIGN_OPTION_HEIGHT: {
		char* end = NULL;
		errno = 0;
		double amount = g_ascii_strtod(value, &end);
		if (!value[0] || g_ascii_isspace(value[0]) || errno || *end ||
		    !isfinite(amount) || amount < 0) {
			return setting_error(
			    error,
			    error_size,
			    "Appearance coordinates and sizes require finite nonnegative "
			    "numbers in PDF points"
			);
		}
		if ((key == SIGN_OPTION_WIDTH || key == SIGN_OPTION_HEIGHT) &&
		    (amount < SIGN_APPEARANCE_MIN_SIZE ||
		     amount > SIGN_APPEARANCE_MAX_SIZE)) {
			return setting_error(
			    error,
			    error_size,
			    "Appearance width and height must be between 12 and 720 PDF "
			    "points"
			);
		}
		if (key == SIGN_OPTION_X) {
			args->appearance.x = amount;
			args->appearance_options |= SIGN_APPEARANCE_X;
		}
		if (key == SIGN_OPTION_Y) {
			args->appearance.y = amount;
			args->appearance_options |= SIGN_APPEARANCE_Y;
		}
		if (key == SIGN_OPTION_WIDTH) {
			args->appearance.width = amount;
			args->appearance.height = 0;
			args->appearance_options |= SIGN_APPEARANCE_WIDTH;
		}
		if (key == SIGN_OPTION_HEIGHT) {
			args->appearance.height = amount;
			args->appearance.width = 0;
			args->appearance_options |= SIGN_APPEARANCE_HEIGHT;
		}
		break;
	}
	case SIGN_OPTION_IMAGE:
		if (!value[0]) {
			return setting_error(
			    error,
			    error_size,
			    "--signature-image requires a nonempty path"
			);
		}
		args->appearance_file = value;
		args->appearance_options |= SIGN_APPEARANCE_FILE;
		break;
	case SIGN_OPTION_INPUT:
		args->input = value;
		break;
	case SIGN_OPTION_OUTPUT:
		args->output = value;
		break;
	case SIGN_OPTION_TOKEN:
		args->uri = value;
		break;
	case SIGN_OPTION_CERT_ID:
		args->id = value;
		break;
	case SIGN_OPTION_ALGORITHM:
		choice = choice_parse(
		    !g_ascii_strcasecmp(value, "rsa") ? "rsa-pkcs" : value,
		    algorithms,
		    3
		);
		if (choice >= 0) {
			args->algo = (libssig_algo)choice;
		}
		break;
	case SIGN_OPTION_HASH:
		choice = choice_parse(value, hashes, 3);
		if (choice >= 0) {
			args->hash = (libssig_hash)choice;
		}
		break;
	case SIGN_OPTION_LEVEL:
		choice = choice_parse(value, levels, 4);
		if (choice >= 0) {
			args->level = (libssig_level)choice;
		}
		break;
	case SIGN_OPTION_FORMAT:
		choice = choice_parse(value, formats, 5);
		if (choice == LIBSSIG_FORMAT_CMS) {
			return setting_error(
			    error,
			    error_size,
			    "Use cades, pades, xades or jades"
			);
		}
		if (choice >= 0) {
			args->format = (libssig_format)choice;
		}
		args->format_explicit = true;
		break;
	case SIGN_OPTION_AUTHORITY:
		args->authority = value;
		break;
	case SIGN_OPTION_CA_FILE:
		args->ca_file = value;
		return true;
	case SIGN_OPTION_INTERMEDIATES:
		args->intermediates = value;
		return true;
	case SIGN_OPTION_TSA_CA_FILE:
		args->tsa_ca_file = value;
		return true;
	case SIGN_OPTION_TSA_INTERMEDIATES:
		args->tsa_intermediates = value;
		return true;
	case SIGN_OPTION_APPEARANCE_INFORMATION:
		args->appearance_information = !strcmp(value, "true");
		return true;
	case SIGN_OPTION_NO_BUNDLED_CERTS:
		args->no_bundled_certs = !strcmp(value, "true");
		return true;
	case SIGN_OPTION_CERT_BUNDLE:
		if (!certs_bundle_known(value)) {
			return setting_error(
			    error,
			    error_size,
			    "Unknown certificate bundle: %s",
			    value
			);
		}
		if (args->cert_bundle_count == G_N_ELEMENTS(args->cert_bundles)) {
			return setting_error(
			    error,
			    error_size,
			    "Too many certificate bundles (maximum 32)"
			);
		}
		args->cert_bundles[args->cert_bundle_count++] = value;
		return true;
	case SIGN_OPTION_DETACHED:
		args->detached = !strcmp(value, "true");
		break;
	case SIGN_OPTION_MIME_TYPE:
		args->mime_type = value;
		break;
	default:
		return setting_error(error, error_size, "Unknown signing option");
	}
	if (choice < 0) {
		return setting_error(error, error_size, "Unknown value: %s", value);
	}
	return true;
}

bool sign_options_validate(const sign_options* args, char* error, size_t size) {
	if (!args || !sign_option_choice(SIGN_OPTION_ALGORITHM, args->algo) ||
	    !sign_option_choice(SIGN_OPTION_HASH, args->hash) ||
	    !sign_option_choice(SIGN_OPTION_LEVEL, args->level) ||
	    !sign_option_choice(SIGN_OPTION_FORMAT, args->format) ||
	    args->format == LIBSSIG_FORMAT_CMS) {
		return setting_error(
		    error,
		    size,
		    "Invalid signing algorithm, hash, level or format"
		);
	}
	if (args->cert_bundle_count > G_N_ELEMENTS(args->cert_bundles) ||
	    (args->modules.count && !args->modules.items)) {
		return setting_error(error, size, "Invalid signing option list");
	}

	if (args->no_bundled_certs && args->cert_bundle_count) {
		return setting_error(
		    error,
		    size,
		    "--no-bundled-certs and --cert-bundle are mutually exclusive"
		);
	}
	if (args->detached && (args->format == LIBSSIG_FORMAT_PADES ||
	                       args->appearance.page_number)) {
		return setting_error(error, size, "PAdES cannot use --detached");
	}
	if (args->appearance.page_number || args->appearance_options) {
		if (!args->appearance.page_number ||
		    (args->appearance_options &
		     (SIGN_APPEARANCE_X | SIGN_APPEARANCE_Y)) !=
		        (SIGN_APPEARANCE_X | SIGN_APPEARANCE_Y)) {
			return setting_error(
			    error,
			    size,
			    "A visible PDF signature requires -n PAGE, -x X and -y Y"
			);
		}
		if ((args->appearance_options &
		     (SIGN_APPEARANCE_WIDTH | SIGN_APPEARANCE_HEIGHT)) ==
		    (SIGN_APPEARANCE_WIDTH | SIGN_APPEARANCE_HEIGHT)) {
			return setting_error(
			    error,
			    size,
			    "Use only --width or --height; the other dimension preserves "
			    "the aspect ratio"
			);
		}
		if (args->format_explicit && args->format != LIBSSIG_FORMAT_PADES) {
			return setting_error(
			    error,
			    size,
			    "A visible signature requires PDF/PAdES"
			);
		}
	}
	return true;
}
