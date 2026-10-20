/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_TIMESTAMP_H
#define SIGN_TIMESTAMP_H
#include <sign/trust.h>
libssig_status http_exchange(
    void*                user,
    libssig_fetch_kind   kind,
    const char*          url,
    const unsigned char* request,
    size_t               request_len,
    libssig_signature*   response
);
bool timestamp_apply(
    const sign_args*   args,
    const sign_trust*  trust,
    libssig_signature* signature
);
#endif
