/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <curl/curl.h>
#include <sign/http.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef SIGN_VERSION
#define SIGN_VERSION "unknown"
#endif

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
	if (!len || len > 16 * 1024 * 1024 - bytes->len) {
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

libssig_status http_exchange(
    void*                user,
    libssig_fetch_kind   kind,
    const char*          url,
    const unsigned char* request,
    size_t               request_len,
    libssig_signature*   response
) {
	(void)user;
	sign_buffer bytes = {0};
	if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
		return LIBSSIG_ERROR_CRYPTO;
	}
	CURL*              curl = curl_easy_init();
	static const char* names[] = {"TSA", "OCSP", "certificate", "CRL"};
	static const char* content_types[] = {
	    "Content-Type: application/timestamp-query",
	    "Content-Type: application/ocsp-request"
	};
	static const char* accept_types[] = {
	    "Accept: application/timestamp-reply",
	    "Accept: application/ocsp-response",
	    "Accept: application/pkix-cert, application/pkcs7-mime, */*",
	    "Accept: application/pkix-crl, */*"
	};
	if ((unsigned int)kind >= sizeof(names) / sizeof(names[0])) {
		curl_easy_cleanup(curl);
		curl_global_cleanup();
		return LIBSSIG_ERROR_ARGUMENT;
	}
	struct curl_slist* headers = curl_slist_append(NULL, accept_types[kind]);
	struct curl_slist* items = headers;
	if (request && kind <= LIBSSIG_FETCH_OCSP) {
		items = headers ? curl_slist_append(headers, content_types[kind])
		                : NULL;
	}
	bool ok = curl && items;
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
	SET(CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
	SET(CURLOPT_FOLLOWLOCATION, 1L);
	SET(CURLOPT_MAXREDIRS, 3L);
	SET(CURLOPT_POSTREDIR, (long)CURL_REDIR_POST_ALL);
	SET(CURLOPT_USERAGENT, "Sign/" SIGN_VERSION);
	SET(CURLOPT_HTTPHEADER, headers);
	if (request) {
		SET(CURLOPT_POSTFIELDS, request);
		SET(CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)request_len);
	}
	SET(CURLOPT_CONNECTTIMEOUT, 10L);
	SET(CURLOPT_TIMEOUT, 45L);
	SET(CURLOPT_NOSIGNAL, 1L);
	SET(CURLOPT_WRITEFUNCTION, response_collect);
	SET(CURLOPT_WRITEDATA, &bytes);
#undef SET
	CURLcode code = ok ? curl_easy_perform(curl) : CURLE_FAILED_INIT;
	long     status = 0;
	ok = code == CURLE_OK &&
	     curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status) == CURLE_OK &&
	     status == 200;
	if (!ok) {
		fprintf(
		    stderr,
		    "sign: %s HTTP failure: %s (HTTP %ld, %s)\n",
		    names[kind],
		    code == CURLE_OK ? "unsuccessful response"
		                     : curl_easy_strerror(code),
		    status,
		    url
		);
	}
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	curl_global_cleanup();
	if (ok) {
		response->data = bytes.data;
		response->len = bytes.len;
	} else {
		free(bytes.data);
	}
	return ok ? LIBSSIG_OK : LIBSSIG_ERROR_TIMESTAMP;
}
