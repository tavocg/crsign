/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_CERTIFICATE_H
#define SIGN_CERTIFICATE_H
#include <libssig/libssig.h>
void certificate_content(const libssig_certificate* cert, bool json);
#endif
