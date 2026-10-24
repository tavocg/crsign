/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_OPTIONS_H
#define SIGN_OPTIONS_H
#include <libssig/libssig.h>
#include <stdbool.h>

/* Borrowed signing settings, shared by the CLI, flows and the signing engine.
 * Zero initialization keeps automatic selection and the existing defaults. */
typedef struct {
	const char *input, *output, *uri, *id, *authority;
	const char *ca_file, *intermediates, *tsa_ca_file, *tsa_intermediates;
	const char* mime_type;
	bool        detached, format_explicit, no_bundled_certs, overwrite;
	libssig_string_list    modules;
	libssig_pdf_appearance appearance;
	const char*            appearance_file;
	bool                   appearance_information;
	unsigned int           appearance_options;
	libssig_algo           algo;
	libssig_hash           hash;
	libssig_level          level;
	libssig_format         format;
	const char*            cert_bundles[32];
	size_t                 cert_bundle_count;
} sign_options;

enum {
	SIGN_APPEARANCE_X = 1u << 0,
	SIGN_APPEARANCE_Y = 1u << 1,
	SIGN_APPEARANCE_WIDTH = 1u << 2,
	SIGN_APPEARANCE_HEIGHT = 1u << 3,
	SIGN_APPEARANCE_FILE = 1u << 4
};
#define SIGN_APPEARANCE_DEFAULT_WIDTH 144.0
#define SIGN_APPEARANCE_MIN_SIZE 12.0
#define SIGN_APPEARANCE_MAX_SIZE 720.0

typedef enum {
	SIGN_OPTION_OUTPUT,
	SIGN_OPTION_TOKEN,
	SIGN_OPTION_CERT_ID,
	SIGN_OPTION_ALGORITHM,
	SIGN_OPTION_HASH,
	SIGN_OPTION_LEVEL,
	SIGN_OPTION_FORMAT,
	SIGN_OPTION_AUTHORITY,
	SIGN_OPTION_PAGE_NUMBER,
	SIGN_OPTION_X,
	SIGN_OPTION_Y,
	SIGN_OPTION_WIDTH,
	SIGN_OPTION_HEIGHT,
	SIGN_OPTION_IMAGE,
	SIGN_OPTION_APPEARANCE_INFORMATION,
	SIGN_OPTION_CA_FILE,
	SIGN_OPTION_INTERMEDIATES,
	SIGN_OPTION_TSA_CA_FILE,
	SIGN_OPTION_TSA_INTERMEDIATES,
	SIGN_OPTION_NO_BUNDLED_CERTS,
	SIGN_OPTION_CERT_BUNDLE,
	SIGN_OPTION_DETACHED,
	SIGN_OPTION_MIME_TYPE,
	SIGN_OPTION_INPUT
} sign_option;

bool sign_options_set(
    sign_options* options,
    sign_option   key,
    const char*   value,
    char*         error,
    size_t        error_size
);
const char* sign_option_choice(sign_option key, unsigned int index);

bool sign_options_validate(
    const sign_options* options,
    char*               error,
    size_t              error_size
);
#endif
