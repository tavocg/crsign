/*
 * crsign - Digital signing application.
 * Copyright (C) 2026 Gustavo Calvo <tavo@tavo.cr>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 * See the LICENSE file in the project root for the full license text.
 */

#include <argp.h>
#include <stdbool.h>

#ifdef SIGN_WITH_GUI
#include "gui.h"
#else
#include <stdio.h>
#include <stdlib.h>
#endif

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


int main(int argc, char** argv) {
	bool gui = false;
	argp_parse(&parser, argc, argv, 0, 0, &gui);

	if (!gui) {
		argp_help(&parser, stdout, ARGP_HELP_STD_HELP, argv[0]);
		return 0;
	}

#ifdef SIGN_WITH_GUI
	return gui_run(argv[0]);
#else
	fprintf(stderr, "sign: GUI unavailable in this headless build\n");
	return EXIT_FAILURE;
#endif
}
