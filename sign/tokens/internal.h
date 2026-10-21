/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_TOKENS_INTERNAL_H
#define SIGN_TOKENS_INTERNAL_H

#include <sign/tokens/tokens.h>

void tokens_output(
    const sign_args*                args,
    libssig_status                  status,
    const libssig_token_list*       tokens,
    const libssig_certificate_list* certs
);

#endif
