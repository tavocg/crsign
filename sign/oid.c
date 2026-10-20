/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/json.h>
#include <sign/oid.h>
#include <stdio.h>
#include <string.h>

/* Known OIDs and display names. Add entries here; unknown OIDs stay numeric.
 * Standard names follow OpenSSL short names. Policy aliases include a country
 * prefix to distinguish national policies with similar descriptions. */
static const struct {
	const char* oid;
	const char* name;
} names[] = {
    /* RFC 5280: extended key usages and anyPolicy. */
    {"1.3.6.1.5.5.7.3.1", "serverAuth"},
    {"1.3.6.1.5.5.7.3.2", "clientAuth"},
    {"1.3.6.1.5.5.7.3.3", "codeSigning"},
    {"1.3.6.1.5.5.7.3.4", "emailProtection"},
    {"1.3.6.1.5.5.7.3.8", "timeStamping"},
    {"1.3.6.1.5.5.7.3.9", "OCSPSigning"},
    {"2.5.29.37.0", "anyExtendedKeyUsage"},
    {"2.5.29.32.0", "anyPolicy"},
    /* Microsoft Smart Card Logon and RFC 4556 PKINIT. */
    {"1.3.6.1.4.1.311.20.2.2", "msSmartcardLogin"},
    {"1.3.6.1.5.2.3.4", "pkInitClientAuth"},
    {"1.3.6.1.5.2.3.5", "pkInitKDC"},
    /* Costa Rica: national certificate policy, section 1.2.
     * https://www.mifirmadigital.go.cr/wp-content/uploads/2022/08/DCFD-Politica-de-certificados-v2.0.pdf
     * These are Sign display aliases, not standardized PKCS#11 names. */
    {"2.16.188.1.1.1.1.1", "cr-ca-emisora"},
    {"2.16.188.1.1.1.1.2", "cr-persona-fisica-firma"},
    {"2.16.188.1.1.1.1.3", "cr-persona-fisica-autenticacion"},
    {"2.16.188.1.1.1.1.5", "cr-sellado-de-tiempo"},
    {"2.16.188.1.1.1.1.6", "cr-persona-juridica-sello"},
    {"2.16.188.1.1.1.1.7", "cr-persona-juridica-agente"},
};

const char* oid_name(const char* oid) {
	if (!oid) {
		return NULL;
	}
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
		if (!strcmp(oid, names[i].oid)) {
			return names[i].name;
		}
	}
	return oid;
}

void oid_names_print(const libssig_string_list* items, bool json) {
	if (json) {
		putchar('[');
	}
	for (size_t i = 0; i < items->count; ++i) {
		if (i) {
			fputs(", ", stdout);
		}
		const char* name = oid_name(items->items[i]);
		if (json) {
			json_string(name);
		} else {
			fputs(name, stdout);
		}
	}
	if (json) {
		putchar(']');
	}
}
