/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_VERIFY_VERIFY_H
#define SIGN_VERIFY_VERIFY_H

#include <sign/arg.h>
#include <sign/trust/trust.h>

bool verify_run(
    const sign_args*   args,
    const sign_buffer* input,
    const sign_trust*  trust
);

#endif
