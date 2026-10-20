/*
 * This file is part of crsign.
 * Copyright (C) 2026 Gustavo Calvo <tavo@tavo.cr>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * See sign/main.c for the full copyright and warranty notice.
 * See LICENSE in the project root for the GNU General Public License.
 */

#ifndef SIGN_ARG_H
#define SIGN_ARG_H

#include <stdbool.h>

bool arg_parse(int argc, char** argv);
void arg_help(char* program_name);

#endif
