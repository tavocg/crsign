/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_SIGN_SIGN_H
#define SIGN_SIGN_SIGN_H

#include <sign/arg.h>
#include <sign/trust/trust.h>

bool sign_create(
    const sign_args*   args,
    const sign_buffer* input,
    const sign_trust*  trust,
    libssig_signature* signature
);

#endif
