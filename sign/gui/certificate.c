/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <glib.h>
#include <sign/gui/certificate.h>
#include <string.h>
/* Decode ASN.1 values rather than parsing the RFC 2253 display string. */
char* sign_certificate_attribute(X509_NAME* name, int nid) {
	if (!name) {
		return NULL;
	}
	GString* text = g_string_new(NULL);
	int      index = -1;
	while ((index = X509_NAME_get_index_by_NID(name, nid, index)) >= 0) {
		ASN1_STRING* value = X509_NAME_ENTRY_get_data(
		    X509_NAME_get_entry(name, index)
		);
		unsigned char* bytes = NULL;
		int            length = ASN1_STRING_to_UTF8(&bytes, value);
		if (length > 0 && !memchr(bytes, 0, length)) {
			char* part = g_utf8_make_valid((const char*)bytes, length);
			g_strstrip(part);
			if (*part) {
				if (text->len) {
					g_string_append(text, " / ");
				}
				g_string_append(text, part);
			}
			g_free(part);
		}
		OPENSSL_free(bytes);
	}
	if (!text->len) {
		g_string_free(text, TRUE);
		return NULL;
	}
	return g_string_free(text, FALSE);
}
char* sign_certificate_display_name(X509_NAME* name) {
	/* RFC 3739 section 3.1.2: commonName is the presentation name; do not
	 * remove issuer-specific suffixes or reinterpret it as a legal name. */
	char* common = sign_certificate_attribute(name, NID_commonName);
	if (common) {
		return common;
	}
	char* pseudonym = sign_certificate_attribute(name, NID_pseudonym);
	if (pseudonym) {
		return pseudonym;
	}
	char* given = sign_certificate_attribute(name, NID_givenName);
	char* surname = sign_certificate_attribute(name, NID_surname);
	char* display = given && surname ? g_strconcat(given, " ", surname, NULL)
	                                 : g_strdup(given ? given : surname);
	g_free(given);
	g_free(surname);
	if (display) {
		return display;
	}
	display = sign_certificate_name(name);
	if (display && !*display) {
		g_free(display);
		return NULL;
	}
	return display;
}
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
