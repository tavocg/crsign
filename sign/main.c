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

#ifdef SIGN_WITH_GUI
#include <sign/gui.h>
#else
#include <stdio.h>
#include <stdlib.h>
#endif

int main(int argc, char** argv) {
	sign_args args = arg_parse(argc, argv);

	if (args.input || args.command == SIGN_COMMAND_TOKENS) {
		return operation_run(&args);
	}

	if (!args.gui) {
		arg_help(argv[0]);
		return 0;
	}

#ifdef SIGN_WITH_GUI
	return gui_run(argv[0]);
#else
	fprintf(stderr, "sign: GUI unavailable in this headless build\n");
	return EXIT_FAILURE;
#endif
}
