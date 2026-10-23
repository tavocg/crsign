/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_TRUST_H
#define SIGN_TRUST_H
#include <glib.h>
#include <sign/io.h>
#include <sign/options.h>
typedef struct {
	sign_buffer ca, intermediates, tsa_ca, tsa_intermediates;
	sign_buffer fallback_ca, tsa_fallback_ca;
} sign_trust;
bool trust_load(const sign_options* args, sign_trust* trust);
bool trust_load_full(
    const sign_options* options,
    sign_trust*         trust,
    GError**            error
);
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
