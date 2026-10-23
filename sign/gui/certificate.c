/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <glib.h>
#include <sign/gui/certificate.h>
char* sign_certificate_name(X509_NAME* name) {
	BIO* bio = BIO_new(BIO_s_mem());
	if (!bio) {
		return NULL;
	}
	char* text = NULL;
	if (name && X509_NAME_print_ex(bio, name, 0, XN_FLAG_RFC2253) >= 0) {
		char* bytes;
		long  length = BIO_get_mem_data(bio, &bytes);
		text = length > 0 ? g_utf8_make_valid(bytes, length) : g_strdup("");
	}
	BIO_free(bio);
	return text;
}
char* sign_certificate_time(const ASN1_TIME* value) {
	struct tm parsed;
	if (!value || ASN1_TIME_to_tm(value, &parsed) != 1) {
		return NULL;
	}
	GDateTime* date = g_date_time_new_utc(
	    parsed.tm_year + 1900,
	    parsed.tm_mon + 1,
	    parsed.tm_mday,
	    parsed.tm_hour,
	    parsed.tm_min,
	    parsed.tm_sec
	);
	if (!date) {
		return NULL;
	}
	char* text = g_date_time_format_iso8601(date);
	g_date_time_unref(date);
	return text;
}
