/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/http.h>
#include <sign/pin.h>
#include <sign/sign.h>
#include <sign/timestamp/timestamp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const libssig_token* token_find(
    const sign_args*          args,
    const libssig_token_list* tokens
) {
	const libssig_token* match = NULL;
	for (size_t i = 0; i < tokens->count; ++i) {
		const libssig_token* token = &tokens->items[i];
		if (args->uri && strcmp(args->uri, token->uri)) {
			continue;
		}
		if (match) {
			fputs("sign: multiple tokens; select one with -t URI\n", stderr);
			return NULL;
		}
		match = token;
	}
	if (!match) {
		fputs("sign: no matching hardware token\n", stderr);
	}
	return match;
}

static bool id_matches(const char* id, const libssig_certificate* cert) {
	const char* digits = "0123456789abcdef";
	if (strlen(id) / 2 != cert->id_len || strlen(id) % 2) {
		return false;
	}
	for (size_t i = 0; i < cert->id_len; ++i) {
		unsigned int high = (unsigned char)id[i * 2];
		unsigned int low = (unsigned char)id[i * 2 + 1];
		if (high >= 'A' && high <= 'F') {
			high += 'a' - 'A';
		}
		if (low >= 'A' && low <= 'F') {
			low += 'a' - 'A';
		}
		if (high != (unsigned int)digits[cert->id[i] >> 4] ||
		    low != (unsigned int)digits[cert->id[i] & 15]) {
			return false;
		}
	}
	return true;
}

static const libssig_certificate* certificate_find(
    const sign_args*                args,
    const libssig_certificate_list* certs
) {
	const libssig_certificate* match = NULL;
	for (size_t i = 0; i < certs->count; ++i) {
		const libssig_certificate* cert = &certs->items[i];
		if (args->id && !id_matches(args->id, cert)) {
			continue;
		}
		if (match) {
			fputs(
			    "sign: multiple certificates; select one with -k ID_HEX\n",
			    stderr
			);
			return NULL;
		}
		match = cert;
	}
	if (!match) {
		fputs("sign: no matching signing certificate\n", stderr);
	}
	return match;
}

static bool pin_required(const libssig_token* token) {
	bool required = false;
	for (size_t i = 0; i < token->flag_names.count; ++i) {
		const char* name = token->flag_names.items[i];
		if (!strcmp(name, "protected-authentication-path")) {
			return false;
		}
		if (!strcmp(name, "login-required")) {
			required = true;
		}
	}
	return required;
}

static libssig_status token_sign(
    const sign_args*           args,
    const sign_buffer*         input,
    const libssig_context*     ctx,
    const sign_trust*          trust,
    const libssig_token*       token,
    const libssig_certificate* cert,
    libssig_signature*         signature
) {
	sign_buffer pin = {0};
	if (pin_required(token) && !pin_read(args->pincmd, &pin)) {
		return LIBSSIG_ERROR_ARGUMENT;
	}
	libssig_verify_params validation = {0};
	trust_verify_params(trust, &validation);
	libssig_sign_services services = {
	    .exchange = http_exchange,
	    .tsa_url = args->authority,
	    .trust = &validation
	};
	libssig_sign_params params = {
	    .token = token,
	    .cert = cert,
	    .data = input->data,
	    .data_len = input->len,
	    .pin = pin.data,
	    .pin_len = pin.len,
	    .level = args->level,
	    .format = args->format,
	    .services = &services,
	    .algo = args->algo,
	    .hash = args->hash,
	    .mime_type = args->mime_type,
	    .options = args->detached ? LIBSSIG_SIGN_DETACHED : 0,
	};
	libssig_status status = libssig_sign(ctx, &params, signature);
	pin_free(&pin);
	return status;
}

bool sign_create(
    const sign_args*   args,
    const sign_buffer* input,
    const sign_trust*  trust,
    libssig_signature* signature
) {
	libssig_context          ctx = {0};
	libssig_token_list       tokens = {0};
	libssig_certificate_list certs = {0};
	libssig_status           status = libssig_context_open(&ctx);
	if (status == LIBSSIG_OK) {
		status = libssig_tokens(&ctx, &tokens, 0);
	}
	const libssig_token* token = status == LIBSSIG_OK
	                                 ? token_find(args, &tokens)
	                                 : NULL;
	if (token) {
		status = libssig_certificates(&ctx, token, &certs, 0);
	}
	const libssig_certificate* cert = token && status == LIBSSIG_OK
	                                      ? certificate_find(args, &certs)
	                                      : NULL;
	if (cert) {
		char*     authority = NULL;
		sign_args resolved = *args;
		if (timestamp_authority(args, trust, cert, &authority)) {
			resolved.authority = authority;
			status = token_sign(
			    &resolved,
			    input,
			    &ctx,
			    trust,
			    token,
			    cert,
			    signature
			);
		} else {
			status = LIBSSIG_ERROR_ARGUMENT;
		}
		free(authority);
	}
	bool ok = cert && status == LIBSSIG_OK;
	if (!ok) {
		fprintf(
		    stderr,
		    "sign: signing failed (status=%d, CK_RV=0x%lx)\n",
		    (int)status,
		    signature->ck_rv
		);
	}
	libssig_certificates_free(&certs);
	libssig_tokens_free(&tokens);
	libssig_context_close(&ctx);
	return ok;
}
