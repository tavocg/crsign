/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/error.h>
#include <sign/execution.h>
#include <sign/history.h>
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
		g_set_error_literal(error, SIGN_ERROR, SIGN_ERROR_INPUT, detail);
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
	if (ok && execution && execution->cancellable) {
		ok = !g_cancellable_set_error_if_cancelled(
		    execution->cancellable,
		    error
		);
	}
	if (ok) {
		ok = file_save(
		    resolved.output,
		    signature.data,
		    signature.len,
		    detail,
		    sizeof(detail)
		);
		if (!ok) {
			g_set_error_literal(error, SIGN_ERROR, SIGN_ERROR_OUTPUT, detail);
		} else {
			result->len = signature.len;
			/* Recording is ancillary: the signed output is already saved. */
			GError* history_error = NULL;
			if (!history_record(resolved.output, &history_error)) {
				g_debug(
				    "Cannot update signing history: %s",
				    history_error ? history_error->message : "unknown error"
				);
				g_clear_error(&history_error);
			}
		}
	}
	libssig_signature_free(&signature);
	trust_free(&trust);
	free(input.data);
	free(default_output);
	return ok;
}

libssig_status sign_verify_buffers(
    const sign_buffer*     input,
    const sign_buffer*     content,
    const sign_trust*      trust,
    libssig_verify_result* result
) {
	libssig_verify_params params = {
	    .data = input->data,
	    .data_len = input->len,
	    .content = content ? content->data : NULL,
	    .content_len = content ? content->len : 0,
	};
	trust_verify_params(trust, &params);
	return libssig_verify(&params, result);
}
bool sign_document_verify(
    const sign_options*    options,
    const char*            content_path,
    libssig_status*        status,
    libssig_verify_result* result,
    GError**               error
) {
	sign_buffer input = {0}, content = {0};
	sign_trust  trust = {0};
	char        detail[512];
	*result = (libssig_verify_result){0};
	*status = LIBSSIG_ERROR_ARGUMENT;
	bool ok = options && options->input;
	if (!ok) {
		g_set_error_literal(
		    error,
		    G_IO_ERROR,
		    G_IO_ERROR_INVALID_ARGUMENT,
		    "An input file is required"
		);
	} else {
		ok = file_load(options->input, &input, detail, sizeof(detail));
		if (ok && content_path) {
			ok = file_load(content_path, &content, detail, sizeof(detail));
		}
		if (!ok) {
			g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, detail);
		}
	}
	if (ok) {
		ok = trust_load_full(options, &trust, error);
	}
	if (ok) {
		*status = sign_verify_buffers(
		    &input,
		    content_path ? &content : NULL,
		    &trust,
		    result
		);
		ok = *status == LIBSSIG_OK;
		if (!ok) {
			g_set_error(
			    error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "Document verification failed (libssig status %d)",
			    (int)*status
			);
		}
	}
	trust_free(&trust);
	free(input.data);
	free(content.data);
	return ok;
}
