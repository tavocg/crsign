/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <limits.h>
#include <openssl/bio.h>
#include <openssl/x509.h>
#include <sign/certificate.h>
#include <sign/json.h>
#include <stdio.h>

static BIO* certificate_decode(const libssig_certificate* cert) {
	if (!cert->der || !cert->der_len || cert->der_len > LONG_MAX) {
		return NULL;
	}
	const unsigned char* cursor = cert->der;
	X509*                x509 = d2i_X509(NULL, &cursor, (long)cert->der_len);
	BIO*                 text = BIO_new(BIO_s_mem());
	if (!x509 || !text || cursor != cert->der + cert->der_len ||
	    X509_print_ex(text, x509, XN_FLAG_RFC2253, X509_FLAG_COMPAT) != 1) {
		BIO_free(text);
		text = NULL;
	}
	X509_free(x509);
	return text;
}

void certificate_content(const libssig_certificate* cert, bool json) {
	BIO*  text = certificate_decode(cert);
	char* data = NULL;
	long  len = text ? BIO_get_mem_data(text, &data) : 0;
	if (json) {
		json_text(
		    len > 0 ? (const unsigned char*)data : NULL,
		    len > 0 ? (size_t)len : 0
		);
	} else if (len > 0) {
		fwrite(data, 1, (size_t)len, stdout);
	} else {
		puts(
		    cert->der && cert->der_len ? "    Certificate content: invalid DER"
		                               : "    Certificate content: unavailable"
		);
	}
	BIO_free(text);
}
