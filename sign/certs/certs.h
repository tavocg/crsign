/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_CERTS_CERTS_H
#define SIGN_CERTS_CERTS_H
#include <sign/trust.h>

typedef enum {
	SIGN_CERT_DOCUMENT = 1u << 0,
	SIGN_CERT_TSA = 1u << 1
} sign_cert_usage;

typedef struct {
	const char*          bundle;
	const char*          sha256;
	const unsigned char* pem;
	size_t               len;
	unsigned int         usage;
	bool                 root;
	const char*          tsa_url;
} sign_cert;

extern const sign_cert sign_certs[];
extern const size_t    sign_certs_count;
bool        certs_bundle_selected(const sign_options* args, const char* name);
bool        certs_bundle_known(const char* name);
bool        certs_load(const sign_options* args, sign_trust* trust);
const char* certs_anchor_bundle(
    const sign_options*            args,
    const libssig_trust_anchor* anchor
);
void certs_anchor_hex(const libssig_trust_anchor* anchor, char hex[65]);
#endif
