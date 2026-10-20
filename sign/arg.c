/*
 * This file is part of crsign.
 * Copyright (C) 2026 Gustavo Calvo <tavo@tavo.cr>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * See sign/main.c for the full copyright and warranty notice.
 * See LICENSE in the project root for the GNU General Public License.
 */

#include "arg.h"

#include <stdio.h>
#include <argp.h>

const char* argp_program_version = "sign " SIGN_VERSION;

static const struct argp_option options[] = {
    {"gui", 'g', 0, 0, "Open the graphical interface", 0},
    {"help", 'h', 0, 0, "Show this help message", 0},
    {0}
};

static error_t parse_option(int key, char* arg, struct argp_state* state) {
	(void)arg;
	bool* gui = state->input;

	switch (key) {
	case 'g':
		*gui = true;
		return 0;
	case 'h':
		argp_state_help(state, stdout, ARGP_HELP_STD_HELP);
		return 0;
	default:
		return ARGP_ERR_UNKNOWN;
	}
}

static const struct argp parser = {
    .options = options,
    .parser = parse_option,
    .doc = "crsign: digital signing application."
};

bool arg_parse(int argc, char** argv) {
	bool gui = false;
	argp_parse(&parser, argc, argv, 0, 0, &gui);
	return gui;
}

void arg_help(char* program_name) {
	argp_help(&parser, stdout, ARGP_HELP_STD_HELP, program_name);
}
