/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/http.h>
#include <sign/timestamp/timestamp.h>
#include <stdio.h>
#include <stdlib.h>

static bool response_fetch(
    const char*              url,
    const libssig_signature* request,
    sign_buffer*             response
) {
	libssig_signature output = {0};
	if (http_exchange(
	        NULL,
	        LIBSSIG_FETCH_TIMESTAMP,
	        url,
	        request->data,
	        request->len,
	        &output
	    ) != LIBSSIG_OK) {
		return false;
	}
	response->data = output.data;
	response->len = output.len;
	return true;
}

bool timestamp_apply(
    const sign_options*   args,
    const sign_trust*  trust,
    libssig_signature* signature
) {
	libssig_timestamp_request_params query = {
	    .data = signature->data,
	    .data_len = signature->len,
	    .hash = args->hash,
	};
	libssig_signature request = {0}, output = {0};
	sign_buffer       response = {0};
	libssig_status    status = libssig_timestamp_request(&query, &request);
	bool              ok = status == LIBSSIG_OK &&
	          response_fetch(args->authority, &request, &response);
	if (ok) {
		libssig_timestamp_params params = {
		    .data = signature->data,
		    .data_len = signature->len,
		    .request = request.data,
		    .request_len = request.len,
		    .response = response.data,
		    .response_len = response.len,
		};
		trust_timestamp_params(trust, &params);
		status = libssig_timestamp(&params, &output);
		ok = status == LIBSSIG_OK;
	}
	if (ok) {
		libssig_signature_free(signature);
		*signature = output;
	} else {
		fprintf(
		    stderr,
		    "sign: timestamp failed (status=%d); no output saved\n",
		    (int)status
		);
		libssig_signature_free(&output);
	}
	libssig_signature_free(&request);
	free(response.data);
	return ok;
}
