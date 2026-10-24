/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <libssig/trust_store.h>
#include <limits.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <sign/gui/error.h>
#include <sign/gui/i18n.h>
#include <sign/gui/inventory.h>
#include <sign/trust.h>

char* sign_device_text(const unsigned char* text, size_t length) {
	while (length && (text[length - 1] == ' ' || text[length - 1] == 0)) {
		--length;
	}
	return g_utf8_make_valid(
	    (const char*)(text ? text : (const unsigned char*)""),
	    length
	);
}
static void certificate_free(gpointer data) {
	sign_device_certificate* cert = data;
	X509_free(cert->certificate);
	g_free(cert);
}
sign_device_inventory* sign_device_inventory_ref(
    sign_device_inventory* inventory
) {
	g_atomic_int_inc(&inventory->references);
	return inventory;
}
void sign_device_inventory_free(sign_device_inventory* inventory) {
	if (!inventory || !g_atomic_int_dec_and_test(&inventory->references)) {
		return;
	}
	g_hash_table_unref(inventory->certificates);
	g_ptr_array_unref(inventory->issues);
	libssig_devices_free(&inventory->devices);
	g_free(inventory);
}
static bool add_certificates(
    X509_STORE* store,
    STACK_OF(X509) * chain,
    const sign_buffer* buffer
) {
	if (!buffer->data || !buffer->len) {
		return true;
	}
	if (buffer->len > INT_MAX) {
		return false;
	}
	BIO* bio = BIO_new_mem_buf(buffer->data, (int)buffer->len);
	if (!bio) {
		return false;
	}
	bool  found = false, ok = true;
	X509* cert;
	while ((cert = PEM_read_bio_X509(bio, NULL, NULL, NULL))) {
		found = true;
		if (store) {
			X509_STORE_add_cert(store, cert);
		}
		if (chain && !sk_X509_push(chain, cert)) {
			X509_free(cert);
			ok = false;
			break;
		} else if (!chain) {
			X509_free(cert);
		}
	}
	BIO_free(bio);
	if (!found) {
		const unsigned char* cursor = buffer->data;
		cert = d2i_X509(NULL, &cursor, (long)buffer->len);
		if (cert && cursor == buffer->data + buffer->len) {
			found = true;
			if (store) {
				X509_STORE_add_cert(store, cert);
			}
			if (chain && !sk_X509_push(chain, cert)) {
				X509_free(cert);
				ok = false;
			} else if (!chain) {
				X509_free(cert);
			}
		} else {
			X509_free(cert);
		}
	}
	ERR_clear_error();
	return found && ok;
}
static int verify(X509_STORE* store, X509* cert, STACK_OF(X509) * chain) {
	if (!store) {
		return -1;
	}
	X509_STORE_CTX* ctx = X509_STORE_CTX_new();
	int             result = -1;
	if (ctx && X509_STORE_CTX_init(ctx, store, cert, chain) == 1) {
		result = X509_verify_cert(ctx) == 1 ? X509_V_OK
		                                    : X509_STORE_CTX_get_error(ctx);
	}
	X509_STORE_CTX_free(ctx);
	ERR_clear_error();
	return result;
}
static void check_trust(
    sign_device_inventory* inventory,
    const sign_options*    options,
    GCancellable*          cancellable
) {
	size_t certificate_count = 0;
	for (size_t m = 0; m < inventory->devices.count; ++m) {
		for (size_t s = 0; s < inventory->devices.items[m].slot_count; ++s) {
			certificate_count += inventory->devices.items[m]
			                         .slots[s]
			                         .certificates.count;
		}
	}
	if (!certificate_count) {
		return;
	}
	X509_STORE* system = X509_STORE_new();
	if (system && libssig_trust_store_load_system(system) != LIBSSIG_OK) {
		X509_STORE_free(system);
		system = NULL;
	}
	X509_STORE* application = X509_STORE_new();
	STACK_OF(X509)* intermediates = sk_X509_new_null();
	sign_trust trust = {0};
	GError*    error = NULL;
	if (!trust_load_full(options, &trust, &error)) {
		g_ptr_array_add(
		    inventory->issues,
		    gui_error_message(error, GUI_ERROR_DEVICES)
		);
		g_clear_error(&error);
		X509_STORE_free(application);
		application = NULL;
	} else if (!add_certificates(application, NULL, &trust.ca) ||
	           !add_certificates(application, NULL, &trust.fallback_ca) ||
	           !add_certificates(NULL, intermediates, &trust.intermediates)) {
		g_ptr_array_add(
		    inventory->issues,
		    g_strdup(_("Cannot read configured certificates"))
		);
		X509_STORE_free(application);
		application = NULL;
	}
	if (!system) {
		g_ptr_array_add(
		    inventory->issues,
		    g_strdup(_("Cannot read system certificates"))
		);
	}
	for (size_t m = 0; m < inventory->devices.count; ++m) {
		libssig_device_module* module = &inventory->devices.items[m];
		for (size_t s = 0;
		     s < module->slot_count && !g_cancellable_is_cancelled(cancellable);
		     ++s) {
			libssig_device_slot* slot = &module->slots[s];
			STACK_OF(X509)* chain = sk_X509_new_null();
			if (!chain) {
				continue;
			}
			for (size_t c = 0; c < slot->certificates.count; ++c) {
				libssig_certificate*     cert = &slot->certificates.items[c];
				sign_device_certificate* annotation = g_new0(
				    sign_device_certificate,
				    1
				);
				annotation->system_trust = annotation->application_trust = -1;
				const unsigned char* cursor = cert->der;
				if (cursor && cert->der_len && cert->der_len <= LONG_MAX) {
					annotation->certificate = d2i_X509(
					    NULL,
					    &cursor,
					    (long)cert->der_len
					);
					if (cursor != cert->der + cert->der_len) {
						X509_free(annotation->certificate);
						annotation->certificate = NULL;
					}
				}
				g_hash_table_insert(inventory->certificates, cert, annotation);
				if (annotation->certificate) {
					sk_X509_push(chain, annotation->certificate);
				}
			}
			for (int n = 0; n < sk_X509_num(intermediates); ++n) {
				sk_X509_push(chain, sk_X509_value(intermediates, n));
			}
			for (size_t c = 0; c < slot->certificates.count; ++c) {
				sign_device_certificate* annotation = g_hash_table_lookup(
				    inventory->certificates,
				    &slot->certificates.items[c]
				);
				if (!annotation->certificate) {
					continue;
				}
				annotation->system_trust = verify(
				    system,
				    annotation->certificate,
				    chain
				);
				annotation->application_trust = verify(
				    application,
				    annotation->certificate,
				    chain
				);
			}
			sk_X509_free(chain);
		}
	}
	sk_X509_pop_free(intermediates, X509_free);
	trust_free(&trust);
	X509_STORE_free(application);
	X509_STORE_free(system);
	ERR_clear_error();
}
sign_device_inventory* sign_device_inventory_scan(
    const libssig_context* context,
    const sign_options*    options,
    unsigned int           device_options,
    GCancellable*          cancellable,
    GError**               error
) {
	sign_device_inventory* inventory = g_new0(sign_device_inventory, 1);
	inventory->references = 1;
	inventory->issues = g_ptr_array_new_with_free_func(g_free);
	inventory->certificates = g_hash_table_new_full(
	    g_direct_hash,
	    g_direct_equal,
	    NULL,
	    certificate_free
	);
	libssig_status status = libssig_devices(
	    context,
	    &inventory->devices,
	    device_options
	);
	if (status != LIBSSIG_OK) {
		g_set_error(
		    error,
		    SIGN_GUI_ERROR,
		    G_IO_ERROR_FAILED,
		    _("Cannot inspect devices (status %d)"),
		    status
		);
		sign_device_inventory_free(inventory);
		return NULL;
	}
	if (g_cancellable_set_error_if_cancelled(cancellable, error)) {
		sign_device_inventory_free(inventory);
		return NULL;
	}
	check_trust(inventory, options, cancellable);
	return inventory;
}
