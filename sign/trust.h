/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_TRUST_H
#define SIGN_TRUST_H
#include <sign/arg.h>
#include <sign/io.h>
typedef struct {
	sign_buffer ca, intermediates, tsa_ca, tsa_intermediates;
} sign_trust;
bool trust_load(const sign_args* args, sign_trust* trust);
void trust_free(sign_trust* trust);
void trust_verify_params(
    const sign_trust*      trust,
    libssig_verify_params* params
);
void trust_timestamp_params(
    const sign_trust*         trust,
    libssig_timestamp_params* params
);
#endif
