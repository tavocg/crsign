/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "internal.h"
#include <stdio.h>
#include <stdlib.h>

int tokens_run(const sign_args* args) {
	libssig_context           ctx = {0};
	libssig_token_list        tokens = {0};
	libssig_certificate_list* certs = NULL;
	libssig_status            status = libssig_context_open(&ctx);
	if (status == LIBSSIG_OK) {
		status = libssig_tokens(&ctx, &tokens, 0);
	}

	if (status == LIBSSIG_OK && tokens.count) {
		certs = calloc(tokens.count, sizeof(*certs));
		if (!certs) {
			status = LIBSSIG_ERROR_MEMORY;
		}
	}
	for (size_t i = 0; status == LIBSSIG_OK && i < tokens.count; ++i) {
		status = libssig_certificates(
		    &ctx,
		    &tokens.items[i],
		    &certs[i],
		    args->all_certificates ? LIBSSIG_CERTIFICATES_ALL : 0
		);
	}
	tokens_output(args, status, &tokens, certs);
	if (certs) {
		for (size_t i = 0; i < tokens.count; ++i) {
			libssig_certificates_free(&certs[i]);
		}
	}
	free(certs);
	libssig_tokens_free(&tokens);
	libssig_context_close(&ctx);
	return status == LIBSSIG_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
