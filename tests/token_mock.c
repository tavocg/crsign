/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <glib.h>
#include <libssig/libssig.h>
#include <sign/io.h>
#include <stdlib.h>
#include <string.h>

libssig_status libssig_context_open(libssig_context* ctx) {
	(void)ctx;
	return getenv("SIGN_MOCK_FAIL") ? LIBSSIG_ERROR_MODULES : LIBSSIG_OK;
}
libssig_status libssig_context_open_modules(
    libssig_context*           ctx,
    const libssig_string_list* paths
) {
	(void)paths;
	return libssig_context_open(ctx);
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
	static const char* pin_flags[] = {
	    "login-required",
	    "protected-authentication-path"
	};
	tokens->count = 1;
	tokens->items[0] = (libssig_token){
	    .uri = "pkcs11:token=Mock",
	    .label = "Tarjeta ñ \"prueba\"\n",
	    .manufacturer = "Test",
	    .model = "Mock",
	    .serial = "123",
	    .flag_names = {
	        pin_flags,
	        getenv("SIGN_MOCK_LOGIN") ? (getenv("SIGN_MOCK_PROTECTED") ? 2 : 1)
	                                  : 0
	    },
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
	static const char* signing_usages[] = {"1.3.6.1.5.5.7.3.4", "1.2.3.999"};
	static const char* authentication_usages[] = {
	    "1.3.6.1.5.5.7.3.2",
	    "1.3.6.1.4.1.311.20.2.2"
	};
	static const char* signing_policies[] = {"2.16.188.1.1.1.1.2", "1.2.3.998"};
	static const char* authentication_policies[] = {"2.16.188.1.1.1.1.3"};
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
		    .extended_key_usage_present = true,
		    .certificate_policies_present = true,
		    .extended_key_usage =
		        {i ? authentication_usages : signing_usages, 2},
		    .certificate_policies =
		        {i ? authentication_policies : signing_policies, i ? 1 : 2},
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
	if (getenv("SIGN_MOCK_LOGIN")) {
		bool        protected = getenv("SIGN_MOCK_PROTECTED") != NULL;
		const char* expected = g_getenv("SIGN_MOCK_EXPECTED_PIN");
		if (!expected) {
			expected = "1234";
		}
		if ((protected && (params->pin || params->pin_len)) ||
		    (!protected &&
		     (!params->pin || params->pin_len != strlen(expected) ||
		      memcmp(params->pin, expected, params->pin_len)))) {
			return LIBSSIG_ERROR_ARGUMENT;
		}
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
	if (params->level != LIBSSIG_LEVEL_B_B) {
		libssig_extend_params extend = {
		    .data = bytes.data,
		    .data_len = bytes.len,
		    .level = params->level,
		    .hash = params->hash,
		    .services = params->services
		};
		libssig_signature output = {0};
		libssig_status    status = libssig_extend(&extend, &output);
		libssig_signature_free(signature);
		if (status == LIBSSIG_OK) {
			*signature = output;
		}
		return status;
	}
	return LIBSSIG_OK;
}
