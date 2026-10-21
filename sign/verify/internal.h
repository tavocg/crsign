/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_VERIFY_INTERNAL_H
#define SIGN_VERIFY_INTERNAL_H

#include <sign/verify/verify.h>

void verify_output(
    const sign_args*             args,
    libssig_status               status,
    const libssig_verify_result* result
);

#endif
