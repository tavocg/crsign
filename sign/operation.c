/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/execution.h>
#include <sign/operation.h>
#include <sign/output.h>
#include <sign/pin.h>
#include <sign/tokens/tokens.h>
#include <sign/verify/verify.h>
#include <stdio.h>
#include <stdlib.h>

static bool cli_pin(
    const libssig_token* token,
    sign_buffer*         pin,
    void*                data,
    GError**             error
) {
	(void)token;
	const sign_args* args = data;
	if (!pin_read(args->pincmd, pin)) {
		g_set_error_literal(
		    error,
		    G_IO_ERROR,
		    G_IO_ERROR_FAILED,
		    "Cannot obtain PIN"
		);
		return false;
	}
	return true;
}

int operation_run(const sign_args* args) {
	if (args->command == SIGN_COMMAND_TOKENS) {
		return tokens_run(args);
	}
	if (args->command == SIGN_COMMAND_VERIFY) {
		sign_buffer input = {0};
		sign_trust  trust = {0};
		bool        ok = trust_load(&args->options, &trust) &&
		          file_read(args->options.input, &input);
		if (ok) {
			ok = verify_run(args, &input, &trust);
		} else {
			operation_output(args, false, 0);
		}
		trust_free(&trust);
		free(input.data);
		return ok ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	sign_args resolved = *args;
	char*     output = args->options.output ? NULL
	                                        : file_signed_path(args->options.input);
	if (!args->options.output && !output) {
		fputs("sign: cannot allocate output path\n", stderr);
		operation_output(args, false, 0);
		return EXIT_FAILURE;
	}
	if (output) {
		resolved.options.output = output;
	}
	sign_execution execution = {.request_pin = cli_pin, .user_data = &resolved};
	sign_document_result result = {0};
	GError*              error = NULL;
	bool                 ok = sign_document_execute(
        &resolved.options,
        &execution,
        &result,
        &error
    );
	if (!ok) {
		fprintf(
		    stderr,
		    "sign: %s\n",
		    error ? error->message : "Signing failed"
		);
	}
	resolved.options.format = result.format;
	operation_output(&resolved, ok, result.len);
	g_clear_error(&error);
	free(output);
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
