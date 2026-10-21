/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/operation.h>
#include <sign/output.h>
#include <sign/sign.h>
#include <sign/tokens/tokens.h>
#include <sign/verify/verify.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
		ok = verify_run(args, &input, &trust);
		reported = true;
	} else if (ok) {
		if (!args->format_explicit && input.len >= 5 &&
		    !memcmp(input.data, "%PDF-", 5)) {
			resolved.format = LIBSSIG_FORMAT_PADES;
		}
		ok = sign_create(args, &input, &trust, &signature);

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
