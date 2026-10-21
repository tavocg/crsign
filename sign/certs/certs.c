/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <openssl/pem.h>
#include <sign/certs/certs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool certs_bundle_known(const char* name) {
	for (size_t i = 0; i < sign_certs_count; ++i) {
		if (!strcmp(name, sign_certs[i].bundle)) {
			return true;
		}
	}
	return false;
}

bool certs_bundle_selected(const sign_args* args, const char* name) {
	if (!args->cert_bundle_count) {
		return true;
	}

	for (size_t i = 0; i < args->cert_bundle_count; ++i) {
		if (!strcmp(name, args->cert_bundles[i])) {
			return true;
		}
	}
	return false;
}

static bool buffer_append(
    sign_buffer*         buffer,
    const unsigned char* data,
    size_t               len
) {
	const size_t max_len = 16u * 1024u * 1024u;
	if (buffer->len > max_len || len > max_len - buffer->len) {
		return false;
	}

	unsigned char* joined = realloc(buffer->data, buffer->len + len);
	if (!joined) {
		return false;
	}
	memcpy(joined + buffer->len, data, len);
	buffer->data = joined;
	buffer->len += len;
	return true;
}

/* User intermediates can be DER. Normalize before joining embedded PEMs. */
static bool intermediates_prepare(sign_buffer* buffer) {
	if (!buffer->len) {
		return true;
	}

	if (buffer->len > 16u * 1024u * 1024u) {
		return false;
	}

	size_t i = 0;
	while (i < buffer->len && strchr(" \t\r\n\f\v", buffer->data[i]) &&
	       buffer->data[i]) {
		++i;
	}
	if (i < buffer->len && buffer->data[i] == '-') {
		return buffer_append(buffer, (const unsigned char*)"\n", 1);
	}

	const unsigned char* cursor = buffer->data;
	X509*                cert = d2i_X509(NULL, &cursor, (long)buffer->len);
	BIO*                 pem = BIO_new(BIO_s_mem());
	bool ok = cert && cursor == buffer->data + buffer->len && pem &&
	          PEM_write_bio_X509(pem, cert);
	char*       data = NULL;
	long        len = ok ? BIO_get_mem_data(pem, &data) : 0;
	sign_buffer result = {0};
	ok = len > 0 &&
	     buffer_append(&result, (const unsigned char*)data, (size_t)len);
	BIO_free(pem);
	X509_free(cert);
	if (ok) {
		free(buffer->data);
		*buffer = result;
	}
	return ok;
}

bool certs_load(const sign_args* args, sign_trust* trust) {
	if (args->no_bundled_certs) {
		return true;
	}

	if (!intermediates_prepare(&trust->intermediates) ||
	    !intermediates_prepare(&trust->tsa_intermediates)) {
		return false;
	}

	for (size_t i = 0; i < sign_certs_count; ++i) {
		const sign_cert* cert = &sign_certs[i];
		if (!certs_bundle_selected(args, cert->bundle)) {
			continue;
		}

		sign_buffer* doc = cert->root ? &trust->fallback_ca
		                              : &trust->intermediates;
		sign_buffer* tsa = cert->root ? &trust->tsa_fallback_ca
		                              : &trust->tsa_intermediates;
		if (((cert->usage & SIGN_CERT_DOCUMENT) &&
		     !buffer_append(doc, cert->pem, cert->len)) ||
		    ((cert->usage & SIGN_CERT_TSA) &&
		     !buffer_append(tsa, cert->pem, cert->len))) {
			return false;
		}
	}
	return true;
}

void certs_anchor_hex(const libssig_trust_anchor* anchor, char hex[65]) {
	static const char digits[] = "0123456789abcdef";
	for (size_t i = 0; i < 32; ++i) {
		hex[2 * i] = digits[anchor->sha256[i] >> 4];
		hex[2 * i + 1] = digits[anchor->sha256[i] & 15];
	}
	hex[64] = 0;
}

const char* certs_anchor_bundle(
    const sign_args*            args,
    const libssig_trust_anchor* anchor
) {
	if (anchor->source != LIBSSIG_TRUST_FALLBACK) {
		return NULL;
	}

	char hex[65];
	certs_anchor_hex(anchor, hex);
	for (size_t i = 0; i < sign_certs_count; ++i) {
		if (sign_certs[i].root &&
		    certs_bundle_selected(args, sign_certs[i].bundle) &&
		    !strcmp(hex, sign_certs[i].sha256)) {
			return sign_certs[i].bundle;
		}
	}
	return NULL;
}
