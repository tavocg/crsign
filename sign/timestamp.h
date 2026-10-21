/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_TIMESTAMP_H
#define SIGN_TIMESTAMP_H

#include <sign/trust.h>

bool timestamp_apply(
    const sign_args*   args,
    const sign_trust*  trust,
    libssig_signature* signature
);

#endif
