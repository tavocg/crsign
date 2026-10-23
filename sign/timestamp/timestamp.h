/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_TIMESTAMP_H
#define SIGN_TIMESTAMP_H

#include <sign/trust.h>

bool timestamp_apply(
    const sign_options*   args,
    const sign_trust*  trust,
    libssig_signature* signature
);

/* Owned URL; explicit authority and B-B require no discovery. */
bool timestamp_authority(
    const sign_options*           args,
    const sign_trust*          trust,
    const libssig_certificate* cert,
    char**                     url
);
#endif
