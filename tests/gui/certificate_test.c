/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <glib.h>
#include <sign/gui/certificate.h>
#include <string.h>

static void add(X509_NAME* name, int nid, const char* value) {
	g_assert_cmpint(
	    X509_NAME_add_entry_by_NID(
	        name,
	        nid,
	        MBSTRING_UTF8,
	        (const unsigned char*)value,
	        -1,
	        -1,
	        0
	    ),
	    ==,
	    1
	);
}
static void expect(X509_NAME* name, const char* expected) {
	char* display = sign_certificate_display_name(name);
	g_assert_cmpstr(display, ==, expected);
	g_free(display);
}
int main(void) {
	expect(NULL, NULL);
	X509_NAME* name = X509_NAME_new();
	g_assert_nonnull(name);
	expect(name, NULL);
	add(name, NID_organizationName, "Example");
	expect(name, "O=Example");
	add(name, NID_surname, "Muñoz");
	expect(name, "Muñoz");
	add(name, NID_givenName, "María");
	expect(name, "María Muñoz");
	add(name, NID_pseudonym, "Alias");
	expect(name, "Alias");
	add(name, NID_commonName, "María Muñoz (FIRMA) <&>, Example");
	expect(name, "María Muñoz (FIRMA) <&>, Example");
	char* full = sign_certificate_name(name);
	g_assert_nonnull(strstr(full, "CN="));
	g_assert_nonnull(strstr(full, "O=Example"));
	g_free(full);
	add(name, NID_commonName, "Other name");
	expect(name, "María Muñoz (FIRMA) <&>, Example / Other name");
	X509_NAME_free(name);
	name = X509_NAME_new();
	add(name, NID_commonName, "   ");
	add(name, NID_givenName, "Single name");
	expect(name, "Single name");
	X509_NAME_free(name);
	return 0;
}
