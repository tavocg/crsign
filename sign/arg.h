/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_ARG_H
#define SIGN_ARG_H

#include <sign/options.h>
#include <stdbool.h>

typedef enum {
	SIGN_COMMAND_SIGN,
	SIGN_COMMAND_VERIFY,
	SIGN_COMMAND_TOKENS
} sign_command;

typedef struct {
	/* Settings borrow storage owned by argv, config and cli_modules below. */
	sign_options        options;
	bool                gui, json, all_certificates, content, signing_options;
	sign_command        command;
	const char* const*  pincmd;
	struct sign_config* config;
	char**              argv;
	char**              cli_modules;
} sign_args;

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
