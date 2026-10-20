/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_OID_H
#define SIGN_OID_H
#include <libssig/libssig.h>
const char* oid_name(const char* oid);
void        oid_names_print(const libssig_string_list* items, bool json);
#endif
