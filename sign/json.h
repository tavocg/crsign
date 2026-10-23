/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_JSON_H
#define SIGN_JSON_H
#include <libssig/libssig.h>
void        json_text(const unsigned char* text, size_t len);
void        json_print_string(const char* text);
void        json_hex(const unsigned char* data, size_t len);
void        json_strings(const libssig_string_list* items);
const char* json_bool(bool value);
#endif
