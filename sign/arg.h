/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_ARG_H
#define SIGN_ARG_H

#include <libssig/libssig.h>
#include <stdbool.h>

typedef struct {
	bool         gui, verify, detached;
	const char * input, *output, *uri, *id, *authority;
	const char * ca_file, *intermediates, *tsa_ca_file, *tsa_intermediates;
	const char*  mime_type;
	libssig_algo algo;
	libssig_hash hash;
} sign_args;

sign_args arg_parse(int argc, char** argv);
void      arg_help(char* program_name);

#endif // SIGN_ARG_H
