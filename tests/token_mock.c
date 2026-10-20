/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <libssig/libssig.h>
#include <sign/io.h>
#include <stdlib.h>
#include <string.h>

libssig_status libssig_context_open(libssig_context* ctx) {
	(void)ctx;
	return getenv("SIGN_MOCK_FAIL") ? LIBSSIG_ERROR_MODULES : LIBSSIG_OK;
}
void libssig_context_close(libssig_context* ctx) {
	(void)ctx;
}
libssig_status libssig_tokens(
    const libssig_context* ctx,
    libssig_token_list*    tokens,
    unsigned int           options
) {
	(void)ctx;
	(void)options;
	if (getenv("SIGN_MOCK_EMPTY")) {
		return LIBSSIG_OK;
	}
	tokens->items = calloc(1, sizeof(*tokens->items));
	if (!tokens->items) {
		return LIBSSIG_ERROR_MEMORY;
	}
	tokens->count = 1;
	tokens->items[0] = (libssig_token){
	    .uri = "pkcs11:token=Mock",
	    .label = "Tarjeta ñ \"prueba\"\n",
	    .manufacturer = "Test",
	    .model = "Mock",
	    .serial = "123",
	};
	return LIBSSIG_OK;
}
void libssig_tokens_free(libssig_token_list* tokens) {
	free(tokens->items);
	*tokens = (libssig_token_list){0};
}
libssig_status libssig_certificates(
    const libssig_context*    ctx,
    const libssig_token*      token,
    libssig_certificate_list* certs,
    unsigned int              options
) {
	(void)ctx;
	(void)token;
	if (getenv("SIGN_MOCK_CERT_FAIL")) {
		return LIBSSIG_ERROR_PKCS11;
	}
	static unsigned char id[] = {0, 0xff};
	static char          label[] = "Firma\0ñ";
	static char          invalid[] = {'x', (char)0xff};
	certs->count = options & LIBSSIG_CERTIFICATES_ALL ? 2 : 1;
	certs->items = calloc(certs->count, sizeof(*certs->items));
	if (!certs->items) {
		certs->count = 0;
		return LIBSSIG_ERROR_MEMORY;
	}
	for (size_t i = 0; i < certs->count; ++i) {
		certs->items[i] = (libssig_certificate){
		    .id = id,
		    .id_len = sizeof(id),
		    .label = i ? invalid : label,
		    .label_len = i ? sizeof(invalid) : sizeof(label) - 1,
		};
		const char* path = getenv("SIGN_MOCK_DER");
		if (path) {
			sign_buffer bytes = {0};
			if (!file_read(path, &bytes)) {
				return LIBSSIG_ERROR_ARGUMENT;
			}
			certs->items[i].der = bytes.data;
			certs->items[i].der_len = bytes.len;
		}
	}
	return LIBSSIG_OK;
}
void libssig_certificates_free(libssig_certificate_list* certs) {
	for (size_t i = 0; i < certs->count; ++i) {
		free(certs->items[i].der);
	}
	free(certs->items);
	*certs = (libssig_certificate_list){0};
}
libssig_status libssig_sign(
    const libssig_context*     ctx,
    const libssig_sign_params* params,
    libssig_signature*         signature
) {
	(void)ctx;
	if (!params->token || !params->cert || !params->data) {
		return LIBSSIG_ERROR_ARGUMENT;
	}
	if (getenv("SIGN_MOCK_SIGN_FAIL")) {
		return LIBSSIG_ERROR_PKCS11;
	}
	const char* path = getenv("SIGN_MOCK_DATA");
	sign_buffer bytes = {0};
	if (!path || !file_read(path, &bytes)) {
		return LIBSSIG_ERROR_ARGUMENT;
	}
	signature->data = bytes.data;
	signature->len = bytes.len;
	return LIBSSIG_OK;
}
