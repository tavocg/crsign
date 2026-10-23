/*
 * Sign: Digital signing application, part of CRSign.
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

#include <sign/arg.h>
#include <sign/operation.h>
#include <sign/runtime.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef SIGN_WITH_GUI
#include <sign/gui.h>
#endif

int main(int argc, char** argv) {
	sign_args args = arg_parse(argc, argv);

	if (args.input || args.command == SIGN_COMMAND_TOKENS) {
		int result = operation_run(&args);
		arg_free(&args);
		return result;
	}

	if (!args.gui) {
		arg_help(args.argv[0]);
		arg_free(&args);
		return 0;
	}

#ifdef SIGN_WITH_GUI
	if (!runtime_init()) {
		arg_free(&args);
		return EXIT_FAILURE;
	}
	int result = gui_run(args.argv[0]);
#else
	fprintf(stderr, "sign: GUI unavailable in this headless build\n");
	int result = EXIT_FAILURE;
#endif
	arg_free(&args);
	return result;
}
