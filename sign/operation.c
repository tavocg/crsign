/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _DEFAULT_SOURCE
#include <sign/operation.h>
#include <sign/output.h>
#include <sign/timestamp.h>
#include <sign/tokens.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
	char* pin = NULL;
	if (pin_required(token)) {
		if (!isatty(STDIN_FILENO)) {
			fputs(
			    "sign: an interactive terminal is required for the PIN\n",
			    stderr
			);
			return LIBSSIG_ERROR_ARGUMENT;
		}
		pin = getpass("PIN: ");
		if (!pin) {
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
	size_t              pin_len = pin ? strlen(pin) : 0;
	libssig_sign_params params = {
	    .token = token,
	    .cert = cert,
	    .data = input->data,
	    .data_len = input->len,
	    .pin = (const unsigned char*)pin,
	    .pin_len = pin_len,
	    .level = args->level,
	    .format = args->format,
	    .services = &services,
	    .algo = args->algo,
	    .hash = args->hash,
	    .mime_type = args->mime_type,
	    .options = args->detached ? LIBSSIG_SIGN_DETACHED : 0,
	};
	libssig_status status = libssig_sign(ctx, &params, signature);
	volatile char* cursor = pin;
	for (size_t i = 0; i < pin_len; ++i) {
		cursor[i] = 0;
	}
	return status;
}

static bool signature_create(
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
		status = token_sign(args, input, &ctx, trust, token, cert, signature);
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

static bool signature_verify(
    const sign_args*   args,
    const sign_buffer* input,
    const sign_trust*  trust
) {
	libssig_verify_params params = {
	    .data = input->data,
	    .data_len = input->len
	};
	trust_verify_params(trust, &params);
	libssig_verify_result result = {0};
	libssig_status        status = libssig_verify(&params, &result);
	verify_output(args, status, &result);
	if (status != LIBSSIG_OK) {
		fprintf(stderr, "sign: verification failed (status=%d)\n", (int)status);
	}
	libssig_verify_free(&result);
	return status == LIBSSIG_OK;
}

int operation_run(const sign_args* args) {
	if (args->command == SIGN_COMMAND_TOKENS) {
		return tokens_run(args);
	}
	sign_args resolved = *args;
	char*     default_output = NULL;
	if (args->command == SIGN_COMMAND_SIGN && !args->output) {
		default_output = file_signed_path(args->input);
		if (!default_output) {
			fputs("sign: cannot allocate output path\n", stderr);
			operation_output(args, false, 0);
			return EXIT_FAILURE;
		}
		resolved.output = default_output;
	}
	args = &resolved;

	sign_buffer       input = {0};
	sign_trust        trust = {0};
	libssig_signature signature = {0};
	bool ok = trust_load(args, &trust) && file_read(args->input, &input);
	bool reported = false;
	if (ok && args->command == SIGN_COMMAND_VERIFY) {
		ok = signature_verify(args, &input, &trust);
		reported = true;
	} else if (ok) {
		if (!args->format_explicit && input.len >= 5 &&
		    !memcmp(input.data, "%PDF-", 5)) {
			resolved.format = LIBSSIG_FORMAT_PADES;
		}
		ok = signature_create(args, &input, &trust, &signature);

		if (ok) {
			ok = file_write(args->output, signature.data, signature.len);
		}
	}
	if (!reported) {
		operation_output(args, ok, signature.len);
	}
	libssig_signature_free(&signature);
	trust_free(&trust);
	free(input.data);
	free(default_output);
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
