/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_GUI_INVENTORY_H
#define SIGN_GUI_INVENTORY_H
#include <gio/gio.h>
#include <openssl/x509.h>
#include <sign/options.h>

typedef struct {
	X509* certificate;
	int   system_trust, application_trust;
} sign_device_certificate;
/* Presentation-only trust annotations over the reusable libssig snapshot. */
typedef struct {
	libssig_device_list devices;
	GPtrArray*          issues;
	GHashTable*         certificates;
} sign_device_inventory;
sign_device_inventory* sign_device_inventory_scan(
    const libssig_context* context,
    const sign_options*    options,
    unsigned int           device_options,
    GCancellable*          cancellable,
    GError**               error
);
void  sign_device_inventory_free(sign_device_inventory* inventory);
char* sign_device_text(const unsigned char* text, size_t length);
#endif
