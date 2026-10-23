/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/execution.h>
#include <sign/sign.h>
#include <stdlib.h>
#include <string.h>

bool sign_document_execute(
    const sign_options*   options,
    const sign_execution* execution,
    sign_document_result* result,
    GError**              error
) {
	sign_document_result local = {0};
	if (!result) {
		result = &local;
	}
	*result = (sign_document_result){0};
	char detail[512];
	if (!options || !options->input || !options->input[0]) {
		g_set_error_literal(
		    error,
		    G_IO_ERROR,
		    G_IO_ERROR_INVALID_ARGUMENT,
		    "An input file is required"
		);
		return false;
	}
	if (!sign_options_validate(options, detail, sizeof(detail))) {
		g_set_error_literal(
		    error,
		    G_IO_ERROR,
		    G_IO_ERROR_INVALID_ARGUMENT,
		    detail
		);
		return false;
	}
	sign_options resolved = *options;
	char*        default_output = options->output ? NULL
	                                              : file_signed_path(options->input);
	if (!options->output && !default_output) {
		g_set_error_literal(
		    error,
		    G_IO_ERROR,
		    G_IO_ERROR_NO_SPACE,
		    "Cannot allocate output path"
		);
		return false;
	}
	if (default_output) {
		resolved.output = default_output;
	}
	sign_buffer       input = {0};
	sign_trust        trust = {0};
	libssig_signature signature = {0};
	bool ok = file_load(resolved.input, &input, detail, sizeof(detail));
	if (!ok) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, detail);
	}
	if (ok && !resolved.format_explicit && input.len >= 5 &&
	    !memcmp(input.data, "%PDF-", 5)) {
		resolved.format = LIBSSIG_FORMAT_PADES;
	}
	result->format = resolved.format;
	if (ok && resolved.appearance.page_number &&
	    (resolved.format != LIBSSIG_FORMAT_PADES || input.len < 5 ||
	     memcmp(input.data, "%PDF-", 5))) {
		g_set_error_literal(
		    error,
		    G_IO_ERROR,
		    G_IO_ERROR_INVALID_ARGUMENT,
		    "A visible signature requires a PDF document"
		);
		ok = false;
	}
	if (ok) {
		ok = trust_load_full(&resolved, &trust, error);
	}
	if (ok) {
		ok = sign_create(
		    &resolved,
		    &input,
		    &trust,
		    &signature,
		    execution,
		    error
		);
	}
	result->ck_rv = signature.ck_rv;
	if (ok) {
		ok = file_save(
		    resolved.output,
		    signature.data,
		    signature.len,
		    detail,
		    sizeof(detail)
		);
		if (!ok) {
			g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, detail);
		} else {
			result->len = signature.len;
		}
	}
	libssig_signature_free(&signature);
	trust_free(&trust);
	free(input.data);
	free(default_output);
	return ok;
}
