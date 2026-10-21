/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_HTTP_H
#define SIGN_HTTP_H

#include <libssig/libssig.h>
#include <sign/io.h>

libssig_status http_exchange(
    void*                user,
    libssig_fetch_kind   kind,
    const char*          url,
    const unsigned char* request,
    size_t               request_len,
    libssig_signature*   response
);

#endif
