/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_SIGN_H
#define SIGN_SIGN_H

#include <sign/execution.h>
#include <sign/options.h>
#include <sign/trust.h>

/* Public identity selection shared by execution and preview. No login or PIN.
 * Keep alive while reading the selected token/certificate; initialize to zero.
 */
typedef struct {
	libssig_context            context;
	libssig_token_list         tokens;
	libssig_certificate_list   certificates;
	const libssig_token*       token;
	const libssig_certificate* certificate;
} sign_identity;
libssig_status sign_identity_open(
    const sign_options* options,
    sign_identity*      identity,
    GError**            error
);
void sign_identity_close(sign_identity* identity);

bool sign_create(
    const sign_options*   args,
    const sign_buffer*    input,
    const sign_trust*     trust,
    libssig_signature*    signature,
    const sign_execution* execution,
    GError**              error
);

#endif
