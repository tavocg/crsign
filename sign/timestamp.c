/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <curl/curl.h>
#include <sign/timestamp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t response_collect(
    char*  data,
    size_t size,
    size_t count,
    void*  user
) {
	sign_buffer* bytes = user;
	if (size && count > SIZE_MAX / size) {
		return 0;
	}
	size_t len = size * count;
	if (!len || len > 1024 * 1024 - bytes->len) {
		return 0;
	}
	unsigned char* items = realloc(bytes->data, bytes->len + len);
	if (!items) {
		return 0;
	}
	bytes->data = items;
	memcpy(bytes->data + bytes->len, data, len);
	bytes->len += len;
	return len;
}

static bool response_fetch(
    const char*              url,
    const libssig_signature* request,
    sign_buffer*             response
) {
	if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
		return false;
	}
	CURL*              curl = curl_easy_init();
	struct curl_slist* headers = curl_slist_append(
	    NULL,
	    "Content-Type: application/timestamp-query"
	);
	struct curl_slist* items = headers
	                               ? curl_slist_append(
	                                     headers,
	                                     "Accept: application/timestamp-reply"
	                                 )
	                               : NULL;
	bool               ok = curl && items;
	if (items) {
		headers = items;
	}
#define SET(option, value)                                                     \
	do {                                                                       \
		if (ok && curl_easy_setopt(curl, option, value) != CURLE_OK) {         \
			ok = false;                                                        \
		}                                                                      \
	} while (0)
	SET(CURLOPT_URL, url);
	SET(CURLOPT_PROTOCOLS_STR, "http,https");
	SET(CURLOPT_HTTPHEADER, headers);
	SET(CURLOPT_POSTFIELDS, request->data);
	SET(CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)request->len);
	SET(CURLOPT_CONNECTTIMEOUT, 10L);
	SET(CURLOPT_TIMEOUT, 45L);
	SET(CURLOPT_NOSIGNAL, 1L);
	SET(CURLOPT_WRITEFUNCTION, response_collect);
	SET(CURLOPT_WRITEDATA, response);
#undef SET
	CURLcode code = ok ? curl_easy_perform(curl) : CURLE_FAILED_INIT;
	long     status = 0;
	ok = code == CURLE_OK &&
	     curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status) == CURLE_OK &&
	     status == 200;
	if (!ok) {
		fprintf(
		    stderr,
		    "sign: TSA HTTP failure: %s (HTTP %ld)\n",
		    curl_easy_strerror(code),
		    status
		);
	}
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	curl_global_cleanup();
	return ok;
}

bool timestamp_apply(
    const sign_args*   args,
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
