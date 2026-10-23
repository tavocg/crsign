/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_SIGN_H
#define SIGN_SIGN_H

#include <sign/execution.h>
#include <sign/options.h>
#include <sign/trust.h>

bool sign_create(
    const sign_options*   args,
    const sign_buffer*    input,
    const sign_trust*     trust,
    libssig_signature*    signature,
    const sign_execution* execution,
    GError**              error
);

#endif
