/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_ARG_H
#define SIGN_ARG_H

#include <libssig/libssig.h>
#include <stdbool.h>

typedef enum {
	SIGN_COMMAND_SIGN,
	SIGN_COMMAND_VERIFY,
	SIGN_COMMAND_TOKENS
} sign_command;

typedef struct {
	bool gui, detached, json, all_certificates, content, signing_options;
	sign_command command;
	const char * input, *output, *uri, *id, *authority;
	const char * ca_file, *intermediates, *tsa_ca_file, *tsa_intermediates;
	const char*  mime_type;
	const char* const*     pincmd;
	libssig_string_list    modules;
	libssig_pdf_appearance appearance;
	const char*            appearance_file;
	unsigned int           appearance_options;
	libssig_algo           algo;
	libssig_hash           hash;
	libssig_level          level;
	libssig_format         format;
	bool                   format_explicit;
	bool                   no_bundled_certs;
	const char*            cert_bundles[32];
	size_t                 cert_bundle_count;
	/* Owns strings read from the selected profile; release with arg_free(). */
	struct sign_config* config;
	/* Owns the native-encoding argument vector (UTF-8 on Windows). */
	char** argv;
	/* Owns normalized CLI module paths; modules borrows this list when set. */
	char** cli_modules;
} sign_args;

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

/* Shared option identifiers and validation for CLI arguments and profiles. */
enum {
	CA_FILE = 256,
	NO_BUNDLED_CERTS,
	CERT_BUNDLE,
	INTERMEDIATES,
	TSA_CA_FILE,
	TSA_INTERMEDIATES,
	DETACHED,
	MIME_TYPE,
	JSON,
	ALL_CERTIFICATES,
	CONTENT,
	HELP
};

bool arg_setting(
    sign_args*  args,
    int         key,
    const char* value,
    char*       error,
    size_t      error_size
);
sign_args arg_parse(int argc, char** argv);
void      arg_free(sign_args* args);
void      arg_help(char* program_name);

#endif // SIGN_ARG_H
