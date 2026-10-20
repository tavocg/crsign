/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_OUTPUT_H
#define SIGN_OUTPUT_H
#include <sign/arg.h>
void verify_output(
    const sign_args*             args,
    libssig_status               status,
    const libssig_verify_result* result
);
void operation_output(const sign_args* args, bool ok, size_t len);
#endif
