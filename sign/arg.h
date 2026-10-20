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
	sign_command   command;
	const char *   input, *output, *uri, *id, *authority;
	const char *   ca_file, *intermediates, *tsa_ca_file, *tsa_intermediates;
	const char*    mime_type;
	libssig_algo   algo;
	libssig_hash   hash;
	libssig_level  level;
	libssig_format format;
	bool           format_explicit;
} sign_args;

sign_args arg_parse(int argc, char** argv);
void      arg_help(char* program_name);

#endif // SIGN_ARG_H
