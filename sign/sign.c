/* SPDX-License-Identifier: GPL-3.0-or-later */
#define G_LOG_DOMAIN "crsign-signing"
#include <glib.h>
#include <p11-kit/pkcs11.h>
#include <sign/appearance.h>
#include <sign/error.h>
#include <sign/http.h>
#include <sign/modules.h>
#include <sign/pin.h>
#include <sign/sign.h>
#include <sign/timestamp/timestamp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const libssig_token* token_find(
    const sign_options*       args,
    const libssig_token_list* tokens,
    GError**                  error
) {
	const libssig_token* match = NULL;
	for (size_t i = 0; i < tokens->count; ++i) {
		const libssig_token* token = &tokens->items[i];
		if (args->uri && strcmp(args->uri, token->uri)) {
			continue;
		}
		if (match) {
			g_set_error_literal(
			    error,
			    SIGN_ERROR,
			    SIGN_ERROR_MANY_TOKENS,
			    "multiple matching tokens; select a token URI"
			);
			return NULL;
		}
		match = token;
	}
	if (!match) {
		g_set_error_literal(
		    error,
		    SIGN_ERROR,
		    SIGN_ERROR_NO_TOKEN,
		    "no matching hardware token"
		);
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
    const sign_options*             args,
    const libssig_certificate_list* certs,
    GError**                        error
) {
	const libssig_certificate* match = NULL;
	for (size_t i = 0; i < certs->count; ++i) {
		const libssig_certificate* cert = &certs->items[i];
		if (args->id && !id_matches(args->id, cert)) {
			continue;
		}
		if (match) {
			g_set_error_literal(
			    error,
			    SIGN_ERROR,
			    SIGN_ERROR_MANY_CERTIFICATES,
			    "multiple matching certificates; select a certificate ID"
			);
			return NULL;
		}
		match = cert;
	}
	if (!match) {
		g_set_error_literal(
		    error,
		    SIGN_ERROR,
		    SIGN_ERROR_NO_CERTIFICATE,
		    "no matching signing certificate"
		);
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
    const sign_options*        args,
    const sign_buffer*         input,
    const sign_buffer*         original,
    const libssig_context*     ctx,
    const sign_trust*          trust,
    const libssig_token*       token,
    const libssig_certificate* cert,
    libssig_signature*         signature,
    const sign_execution*      execution,
    GError**                   error
) {
	g_debug("stage=appearance phase=begin");
	libssig_pdf_appearance appearance = {0};
	unsigned char*         pixels = NULL;
	if (!appearance_prepare(args, cert, &appearance, &pixels)) {
		g_set_error_literal(
		    error,
		    SIGN_ERROR,
		    SIGN_ERROR_IMAGE,
		    "Cannot prepare signature appearance"
		);
		return LIBSSIG_ERROR_ARGUMENT;
	}
	g_debug("stage=appearance phase=end ok=1");
	sign_buffer pin = {0};
	if (pin_required(token)) {
		g_debug("stage=pin-provider phase=begin");
		bool ok = execution && execution->request_pin;
		if (!ok) {
			g_set_error_literal(
			    error,
			    SIGN_ERROR,
			    SIGN_ERROR_PIN,
			    "A PIN provider is required at execution time"
			);
		}
		if (ok) {
			ok = execution
			         ->request_pin(token, &pin, execution->user_data, error);
		}
		if (ok && (!pin.data || pin.len > SIGN_PIN_LIMIT)) {
			g_set_error_literal(
			    error,
			    SIGN_ERROR,
			    SIGN_ERROR_PIN,
			    "Invalid PIN provider response"
			);
			ok = false;
		}
		g_debug("stage=pin-provider phase=end ok=%d", ok);
		if (!ok) {
			if (error && !*error) {
				g_set_error_literal(
				    error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "Cannot obtain PIN"
				);
			}
			pin_free(&pin);
			g_free(pixels);
			return LIBSSIG_ERROR_ARGUMENT;
		}
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
	    .content = original->data,
	    .content_len = original->len,
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
	g_debug(
	    "stage=libssig-sign phase=begin format=%d level=%d",
	    (int)args->format,
	    (int)args->level
	);
	libssig_status
	    status = appearance.page_number
	                 ? libssig_sign_pdf(ctx, &params, &appearance, signature)
	                 : libssig_sign(ctx, &params, signature);
	g_debug(
	    "stage=libssig-sign phase=end status=%d CK_RV=0x%lx",
	    (int)status,
	    signature->ck_rv
	);
	g_free(pixels);
	pin_free(&pin);
	return status;
}

void sign_identity_close(sign_identity* identity) {
	libssig_certificates_free(&identity->certificates);
	libssig_tokens_free(&identity->tokens);
	g_debug("stage=context-close phase=begin");
	libssig_context_close(&identity->context);
	g_debug("stage=context-close phase=end");
	*identity = (sign_identity){0};
}
libssig_status sign_identity_open(
    const sign_options* options,
    sign_identity*      identity,
    GError**            error
) {
	g_debug("stage=modules-open phase=begin");
	libssig_status status = modules_open(options, &identity->context);
	g_debug("stage=modules-open phase=end status=%d", (int)status);
	if (status != LIBSSIG_OK) {
		g_set_error_literal(
		    error,
		    SIGN_ERROR,
		    SIGN_ERROR_MODULES,
		    "Cannot load signing modules"
		);
	}
	if (status == LIBSSIG_OK) {
		g_debug("stage=tokens phase=begin");
		status = libssig_tokens(&identity->context, &identity->tokens, 0);
		g_debug(
		    "stage=tokens phase=end status=%d count=%zu",
		    (int)status,
		    identity->tokens.count
		);
	}
	if (status == LIBSSIG_OK) {
		identity->token = token_find(options, &identity->tokens, error);
		if (!identity->token) {
			status = LIBSSIG_ERROR_NOT_FOUND;
		}
	}
	if (status == LIBSSIG_OK) {
		g_debug("stage=certificates phase=begin");
		status = libssig_certificates(
		    &identity->context,
		    identity->token,
		    &identity->certificates,
		    0
		);
		g_debug(
		    "stage=certificates phase=end status=%d count=%zu",
		    (int)status,
		    identity->certificates.count
		);
	}
	if (status == LIBSSIG_OK) {
		identity->certificate = certificate_find(
		    options,
		    &identity->certificates,
		    error
		);
		if (!identity->certificate) {
			status = LIBSSIG_ERROR_NOT_FOUND;
		}
	}
	return status;
}
bool sign_create(
    const sign_options*   args,
    const sign_buffer*    input,
    const sign_trust*     trust,
    libssig_signature*    signature,
    const sign_execution* execution,
    GError**              error
) {
	sign_buffer original = {0};
	char        message[512];
	if (args->original &&
	    !file_load(args->original, &original, message, sizeof(message))) {
		g_set_error_literal(error, SIGN_ERROR, SIGN_ERROR_INPUT, message);
		return false;
	}
	if (args->extend) {
		libssig_verify_params validation = {0};
		trust_verify_params(trust, &validation);
		libssig_sign_services services = {
		    .exchange = http_exchange,
		    .tsa_url = args->authority,
		    .trust = &validation
		};
		libssig_extend_params params = {
		    .data = input->data,
		    .data_len = input->len,
		    .content = original.data,
		    .content_len = original.len,
		    .level = args->level,
		    .hash = args->hash,
		    .services = &services
		};
		g_debug("stage=libssig-extend phase=begin level=%d", (int)args->level);
		libssig_status status = libssig_extend(&params, signature);
		g_debug("stage=libssig-extend phase=end status=%d", (int)status);
		free(original.data);
		if (status != LIBSSIG_OK) {
			g_set_error(
			    error,
			    SIGN_ERROR,
			    SIGN_ERROR_CRYPTO,
			    "Evidence renewal failed (status=%d); select a TSA URL and "
			    "provide the original content for detached signatures",
			    (int)status
			);
		}
		return status == LIBSSIG_OK;
	}
	sign_identity        identity = {0};
	libssig_status       status = sign_identity_open(args, &identity, error);
	const libssig_token* token = identity.token;
	const libssig_certificate* cert = identity.certificate;
	if (cert) {
		char*        authority = NULL;
		sign_options resolved = *args;
		g_debug("stage=authority-selection phase=begin");
		if (timestamp_authority(args, trust, cert, &authority)) {
			g_debug("stage=authority-selection phase=end ok=1");
			resolved.authority = authority;
			status = token_sign(
			    &resolved,
			    input,
			    &original,
			    &identity.context,
			    trust,
			    token,
			    cert,
			    signature,
			    execution,
			    error
			);
		} else {
			g_debug("stage=authority-selection phase=end ok=0");
			status = LIBSSIG_ERROR_ARGUMENT;
		}
		free(authority);
	}
	sign_error_code code = SIGN_ERROR_CRYPTO;
	switch (status) {
	case LIBSSIG_ERROR_MODULES:
	case LIBSSIG_ERROR_PKCS11:
		code = SIGN_ERROR_MODULES;
		break;
	case LIBSSIG_ERROR_TRUST:
	case LIBSSIG_ERROR_REVOKED:
		code = SIGN_ERROR_TRUST;
		break;
	case LIBSSIG_ERROR_TIMESTAMP:
	case LIBSSIG_ERROR_OCSP:
	case LIBSSIG_ERROR_EVIDENCE:
		code = SIGN_ERROR_TIMESTAMP;
		break;
	case LIBSSIG_ERROR_UNSUPPORTED:
	case LIBSSIG_ERROR_FORMAT:
		code = SIGN_ERROR_UNSUPPORTED;
		break;
	default:
		break;
	}
	if (signature->ck_rv == CKR_PIN_INCORRECT) {
		code = SIGN_ERROR_PIN_INCORRECT;
	} else if (signature->ck_rv == CKR_PIN_LOCKED) {
		code = SIGN_ERROR_PIN_LOCKED;
	} else if (signature->ck_rv == CKR_TOKEN_NOT_PRESENT ||
	           signature->ck_rv == CKR_DEVICE_REMOVED) {
		code = SIGN_ERROR_DEVICE_REMOVED;
	}
	bool ok = cert && status == LIBSSIG_OK;
	if (!ok && error && !*error) {
		g_set_error(
		    error,
		    SIGN_ERROR,
		    code,
		    "Signing failed (status=%d, CK_RV=0x%lx)",
		    (int)status,
		    signature->ck_rv
		);
	}
	g_debug(
	    "stage=sign-create phase=end status=%d CK_RV=0x%lx",
	    (int)status,
	    signature->ck_rv
	);
	sign_identity_close(&identity);
	free(original.data);
	return ok;
}
