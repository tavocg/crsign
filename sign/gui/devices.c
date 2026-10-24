/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <openssl/x509v3.h>
#include <p11-kit/p11-kit.h>
#include <sign/config.h>
#include <sign/gui/certificate.h>
#include <sign/gui/devices.h>
#include <sign/gui/error.h>
#include <sign/gui/i18n.h>
#include <sign/gui/inventory.h>
#include <sign/modules.h>
#include <stdint.h>
#include <string.h>

struct _SignDevices {
	GtkBox        parent_instance;
	GtkWidget *   refresh, *spinner, *content, *body, *header_menu;
	GtkLabel*     status;
	GCancellable* cancellable;
	gint          revision, include_software;
	bool          busy, monitoring, closing;
};
G_DEFINE_TYPE(SignDevices, sign_devices, GTK_TYPE_BOX)
enum { PROP_0, PROP_BUSY, N_PROPERTIES };
static GParamSpec* properties[N_PROPERTIES];
static const char* yes_no(bool value) {
	return value ? _("Yes") : _("No");
}
static AdwActionRow* field(
    AdwExpanderRow* parent,
    const char*     name,
    const char*     value
) {
	AdwActionRow* row = ADW_ACTION_ROW(adw_action_row_new());
	adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), name);
	char* safe = value && *value ? g_utf8_make_valid(value, -1)
	                             : g_strdup(_("Unavailable"));
	adw_action_row_set_subtitle(row, safe);
	g_free(safe);
	adw_action_row_set_subtitle_lines(row, 0);
	adw_preferences_row_set_title_selectable(ADW_PREFERENCES_ROW(row), TRUE);
	adw_expander_row_add_row(parent, GTK_WIDGET(row));
	return row;
}
static void number(
    AdwExpanderRow* parent,
    const char*     name,
    unsigned long   value
) {
	char* text = value == CK_UNAVAILABLE_INFORMATION
	                 ? g_strdup(_("Unavailable"))
	                 : g_strdup_printf("%lu", value);
	field(parent, name, text);
	g_free(text);
}
static AdwExpanderRow* expander(const char* title, const char* icon) {
	AdwExpanderRow* row = ADW_EXPANDER_ROW(adw_expander_row_new());
	adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
	adw_expander_row_add_prefix(row, gtk_image_new_from_icon_name(icon));
	return row;
}
/* Keep collapsed rows cheap, retaining only their immutable scan snapshot. */
typedef void (*populate_details)(
    AdwExpanderRow*,
    gpointer,
    sign_device_inventory*
);
typedef struct {
	populate_details       populate;
	gpointer               object;
	sign_device_inventory* inventory;
	GtkWidget*             placeholder;
	bool                   populated;
} lazy_details;
static void lazy_details_free(gpointer data, GClosure* closure) {
	(void)closure;
	lazy_details* details = data;
	sign_device_inventory_free(details->inventory);
	g_free(details);
}
static void expand_details(
    AdwExpanderRow* row,
    GParamSpec*     pspec,
    gpointer        data
) {
	(void)pspec;
	lazy_details* details = data;
	if (details->populated || !adw_expander_row_get_expanded(row)) {
		return;
	}
	details->populated = true;
	details->populate(row, details->object, details->inventory);
	adw_expander_row_remove(row, details->placeholder);
	details->placeholder = NULL;
}
static void defer_details(
    AdwExpanderRow*        row,
    gpointer               object,
    sign_device_inventory* inventory,
    populate_details       populate
) {
	lazy_details* details = g_new0(lazy_details, 1);
	details->object = object;
	details->inventory = sign_device_inventory_ref(inventory);
	details->populate = populate;
	/* An empty child keeps the expander enabled before its details exist. */
	details->placeholder = gtk_list_box_row_new();
	gtk_list_box_row_set_activatable(
	    GTK_LIST_BOX_ROW(details->placeholder),
	    FALSE
	);
	gtk_list_box_row_set_selectable(
	    GTK_LIST_BOX_ROW(details->placeholder),
	    FALSE
	);
	adw_expander_row_add_row(row, details->placeholder);
	g_signal_connect_data(
	    row,
	    "notify::expanded",
	    G_CALLBACK(expand_details),
	    details,
	    lazy_details_free,
	    0
	);
}
static const char* object_kind(CK_OBJECT_CLASS kind) {
	switch (kind) {
	case CKO_CERTIFICATE:
		return _("Certificate");
	case CKO_PUBLIC_KEY:
		return _("Public key");
	case CKO_PRIVATE_KEY:
		return _("Private key");
	default:
		return _("Secret key");
	}
}
static const char* key_type(CK_KEY_TYPE type) {
	switch (type) {
	case CKK_RSA:
		return "RSA";
	case CKK_EC:
		return "EC";
	case CKK_DSA:
		return "DSA";
	case CKK_AES:
		return "AES";
	default:
		return _("Unknown");
	}
}
static char* hex_id(const unsigned char* bytes, size_t size) {
	GString* hex = g_string_new(NULL);
	for (size_t n = 0; n < size; ++n) {
		g_string_append_printf(hex, "%02x", bytes[n]);
	}
	return g_string_free(hex, FALSE);
}
static void render_key(AdwExpanderRow* parent, libssig_device_key* key) {
	const char* kind = object_kind(key->object_class);
	char*       label = sign_device_text(
        (const unsigned char*)key->label,
        key->label_len
    );
	AdwExpanderRow* row = expander(
	    *label ? label : kind,
	    "dialog-password-symbolic"
	);
	g_free(label);
	adw_expander_row_set_subtitle(row, kind);
	adw_expander_row_add_row(parent, GTK_WIDGET(row));
	field(row, _("Object type"), kind);
	char* id = hex_id(key->id, key->id_len);
	field(row, _("ID"), id);
	g_free(id);
	char* size = key->bits ? g_strdup_printf(_("%lu bits"), key->bits) : NULL;
	field(
	    row,
	    key->size_from_certificate ? _("Key size (associated certificate)")
	                               : _("Key size"),
	    size
	);
	g_free(size);
	field(row, _("Key type"), key_type(key->key_type));
	if (key->key_type != CKK_RSA && key->key_type != CKK_EC &&
	    key->key_type != CKK_DSA && key->key_type != CKK_AES) {
		number(row, _("PKCS#11 key type"), key->key_type);
	}
	field(
	    row,
	    _("Signing attribute"),
	    key->sign_known ? yes_no(key->can_sign) : _("Unavailable")
	);
}
static void certificate_details(
    AdwExpanderRow*        row,
    gpointer               data,
    sign_device_inventory* inventory
) {
	libssig_certificate* object = data;
	char*                id = hex_id(object->id, object->id_len);
	field(row, _("ID"), id);
	g_free(id);
	char* size = object->public_key_bits
	                 ? g_strdup_printf(_("%lu bits"), object->public_key_bits)
	                 : NULL;
	field(row, _("Key size"), size);
	g_free(size);
	sign_device_certificate* annotation = g_hash_table_lookup(
	    inventory->certificates,
	    object
	);
	X509* cert = annotation ? annotation->certificate : NULL;
	if (!cert) {
		field(
		    row,
		    _("Certificate data"),
		    _("Unavailable or invalid X.509 certificate")
		);
		return;
	}
	EVP_PKEY* key = X509_get_pubkey(cert);
	field(
	    row,
	    _("Public key algorithm"),
	    key ? OBJ_nid2ln(EVP_PKEY_base_id(key)) : NULL
	);
	EVP_PKEY_free(key);
	char* subject = sign_certificate_name(X509_get_subject_name(cert));
	char* issuer = sign_certificate_name(X509_get_issuer_name(cert));
	char* from = sign_certificate_time(X509_get0_notBefore(cert));
	char* until = sign_certificate_time(X509_get0_notAfter(cert));
	field(row, _("Certificate subject"), subject);
	field(row, _("Certificate issuer"), issuer);
	field(row, _("Valid from"), from);
	field(row, _("Valid until"), until);
	g_free(subject);
	g_free(issuer);
	g_free(from);
	g_free(until);
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int  length;
	if (X509_digest(cert, EVP_sha256(), digest, &length) == 1) {
		GString* hex = g_string_new(NULL);
		for (unsigned n = 0; n < length; ++n) {
			g_string_append_printf(hex, n ? ":%02X" : "%02X", digest[n]);
		}
		field(row, _("Certificate SHA-256"), hex->str);
		g_string_free(hex, TRUE);
	}
	uint32_t usage = X509_get_key_usage(cert);
	field(
	    row,
	    _("Content commitment key usage"),
	    usage == UINT32_MAX ? _("Not specified")
	                        : yes_no((usage & KU_NON_REPUDIATION) != 0)
	);
	field(
	    row,
	    _("System certificates"),
	    gui_trust_error(annotation->system_trust)
	);
	field(
	    row,
	    _("CRSign certificates (embedded / configured)"),
	    gui_trust_error(annotation->application_trust)
	);
}

static void render_certificate(
    AdwExpanderRow*        parent,
    libssig_certificate*   object,
    sign_device_inventory* inventory
) {
	char* label = sign_device_text(
	    (const unsigned char*)object->label,
	    object->label_len
	);
	AdwExpanderRow* row = expander(
	    *label ? label : _("Certificate"),
	    "application-certificate-symbolic"
	);
	g_free(label);
	adw_expander_row_set_subtitle(row, _("Certificate"));
	adw_expander_row_add_row(parent, GTK_WIDGET(row));
	gtk_widget_set_name(GTK_WIDGET(row), "devices-certificate");
	defer_details(row, object, inventory, certificate_details);
}

static void copy_token_uri(GtkButton* button, gpointer data) {
	(void)data;
	const char* uri = g_object_get_data(G_OBJECT(button), "token-uri");
	if (uri) {
		gdk_clipboard_set_text(
		    gtk_widget_get_clipboard(GTK_WIDGET(button)),
		    uri
		);
	}
}
static void slot_details(
    AdwExpanderRow*        row,
    gpointer               data,
    sign_device_inventory* inventory
) {
	libssig_device_slot* slot = data;
	number(row, _("Slot ID"), slot->id);
	field(row, _("Hardware slot"), yes_no((slot->flags & CKF_HW_SLOT) != 0));
	field(
	    row,
	    _("Removable device"),
	    yes_no((slot->flags & CKF_REMOVABLE_DEVICE) != 0)
	);
	if (slot->status != LIBSSIG_OK) {
		field(row, _("Device error"), gui_device_error(slot->ck_rv));
	}
	if (!slot->token_valid) {
		return;
	}
	libssig_token* token = &slot->token;
	field(row, _("Token label"), token->label);
	AdwActionRow* uri_row = field(row, _("Token URI"), token->uri);
	GtkWidget*    copy = gtk_button_new_from_icon_name("edit-copy-symbolic");
	gtk_widget_set_name(copy, "devices-copy-token-uri");
	gtk_widget_set_tooltip_text(copy, _("Copy Token URI"));
	gtk_widget_add_css_class(copy, "flat");
	gtk_widget_set_valign(copy, GTK_ALIGN_CENTER);
	gtk_widget_set_sensitive(copy, token->uri && *token->uri);
	g_object_set_data_full(
	    G_OBJECT(copy),
	    "token-uri",
	    g_strdup(token->uri),
	    g_free
	);
	g_signal_connect(copy, "clicked", G_CALLBACK(copy_token_uri), NULL);
	adw_action_row_add_suffix(uri_row, copy);
	field(row, _("Manufacturer"), token->manufacturer);
	field(row, _("Model"), token->model);
	field(row, _("Serial number"), token->serial);
	char* version = g_strdup_printf(
	    "%u.%u",
	    token->hardware_version.major,
	    token->hardware_version.minor
	);
	field(row, _("Hardware version"), version);
	g_free(version);
	version = g_strdup_printf(
	    "%u.%u",
	    token->firmware_version.major,
	    token->firmware_version.minor
	);
	field(row, _("Firmware version"), version);
	g_free(version);
	field(
	    row,
	    _("Login required"),
	    yes_no((token->flags & CKF_LOGIN_REQUIRED) != 0)
	);
	field(
	    row,
	    _("Protected PIN entry"),
	    yes_no((token->flags & CKF_PROTECTED_AUTHENTICATION_PATH) != 0)
	);
	field(
	    row,
	    _("PIN locked"),
	    yes_no((token->flags & CKF_USER_PIN_LOCKED) != 0)
	);
	field(
	    row,
	    _("PIN final attempt"),
	    yes_no((token->flags & CKF_USER_PIN_FINAL_TRY) != 0)
	);
	number(row, _("Minimum PIN length"), token->min_pin_len);
	number(row, _("Maximum PIN length"), token->max_pin_len);
	number(row, _("Public memory (bytes)"), token->total_public_memory);
	number(row, _("Free public memory (bytes)"), token->free_public_memory);
	number(row, _("Private memory (bytes)"), token->total_private_memory);
	number(row, _("Free private memory (bytes)"), token->free_private_memory);
	number(
	    row,
	    _("Visible objects"),
	    slot->certificates.count + slot->key_count
	);
	for (size_t c = 0; c < slot->certificates.count; ++c) {
		render_certificate(row, &slot->certificates.items[c], inventory);
	}
	for (size_t k = 0; k < slot->key_count; ++k) {
		render_key(row, &slot->keys[k]);
	}
}
static void render_slot(
    AdwExpanderRow*        parent,
    libssig_device_slot*   slot,
    sign_device_inventory* inventory
) {
	char*           title = g_strdup_printf(_("Slot %lu"), slot->id);
	AdwExpanderRow* row = expander(title, "media-flash-symbolic");
	g_free(title);
	char* label = slot->token_valid ? g_utf8_make_valid(slot->token.label, -1)
	                                : NULL;
	adw_expander_row_set_subtitle(
	    row,
	    slot->token_present ? (label && *label ? label : _("Token present"))
	                        : _("No token inserted")
	);
	g_free(label);
	adw_expander_row_add_row(parent, GTK_WIDGET(row));
	gtk_widget_set_name(GTK_WIDGET(row), "devices-slot");
	defer_details(row, slot, inventory, slot_details);
}
static void clear_content(SignDevices* self) {
	GtkWidget* child;
	while ((child = gtk_widget_get_first_child(self->content))) {
		gtk_box_remove(GTK_BOX(self->content), child);
	}
}
static void render(SignDevices* self, sign_device_inventory* inventory) {
	clear_content(self);
	size_t readers = 0, slots = 0, certificates = 0, keys = 0;
	for (size_t m = 0; m < inventory->devices.count; ++m) {
		libssig_device_module* module = &inventory->devices.items[m];
		if (module->status != LIBSSIG_OK) {
			char* issue = g_strdup_printf(
			    "%s: %s",
			    module->name ? module->name : _("PKCS#11 module"),
			    gui_device_error(module->ck_rv)
			);
			g_ptr_array_add(inventory->issues, issue);
		}
		GHashTable* groups = g_hash_table_new_full(
		    g_str_hash,
		    g_str_equal,
		    g_free,
		    NULL
		);
		for (size_t s = 0; s < module->slot_count; ++s) {
			libssig_device_slot* slot = &module->slots[s];
			char*                description = g_utf8_make_valid(
                slot->description ? slot->description : "",
			    -1
			);
			char* group_id = g_strdup_printf(
			    "%s/%s",
			    slot->manufacturer ? slot->manufacturer : "",
			    description
			);
			AdwExpanderRow* row = g_hash_table_lookup(groups, group_id);
			if (!row) {
				++readers;
				row = expander(
				    *description ? description : _("Unnamed reader"),
				    "drive-removable-media-symbolic"
				);
				char* module_name = g_utf8_make_valid(
				    module->name ? module->name : "",
				    -1
				);
				adw_expander_row_set_subtitle(row, module_name);
				g_free(module_name);
				/* Devices start collapsed; details are available on demand. */
				GtkWidget* group = adw_preferences_group_new();
				adw_preferences_group_add(
				    ADW_PREFERENCES_GROUP(group),
				    GTK_WIDGET(row)
				);
				gtk_box_append(GTK_BOX(self->content), group);
				g_hash_table_insert(groups, g_strdup(group_id), row);
				field(row, _("Manufacturer"), slot->manufacturer);
				field(row, _("PKCS#11 module"), module->name);
				field(row, _("Module path"), module->path);
				field(row, _("Library"), module->description);
				char* version = g_strdup_printf(
				    "%u.%u",
				    module->version.major,
				    module->version.minor
				);
				field(row, _("Library version"), version);
				g_free(version);
				version = g_strdup_printf(
				    "%u.%u",
				    module->cryptoki_version.major,
				    module->cryptoki_version.minor
				);
				field(row, _("PKCS#11 version"), version);
				g_free(version);
			}
			g_free(description);
			g_free(group_id);
			++slots;
			certificates += slot->certificates.count;
			keys += slot->key_count;
			render_slot(row, slot, inventory);
		}
		g_hash_table_unref(groups);
	}
	char* summary = readers ? g_strdup_printf(
	                              _("Readers: %zu · Slots: %zu · Certificates: "
	                                "%zu · Keys: %zu"),
	                              readers,
	                              slots,
	                              certificates,
	                              keys
	                          )
	                        : g_strdup(_("No readers found"));
	gtk_label_set_text(self->status, summary);
	g_free(summary);
	for (guint n = 0; n < inventory->issues->len; ++n) {
		GtkWidget* label = gtk_label_new(
		    g_ptr_array_index(inventory->issues, n)
		);
		gtk_label_set_wrap(GTK_LABEL(label), TRUE);
		gtk_widget_add_css_class(label, "error");
		gtk_box_append(GTK_BOX(self->content), label);
	}
}
static void set_busy(SignDevices* self, bool busy) {
	self->busy = busy;
	gtk_widget_set_sensitive(self->refresh, !busy);
	gtk_widget_set_visible(self->spinner, busy);
	if (busy) {
		gtk_spinner_start(GTK_SPINNER(self->spinner));
	} else {
		gtk_spinner_stop(GTK_SPINNER(self->spinner));
	}
	g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_BUSY]);
}
typedef struct {
	SignDevices*           self;
	sign_device_inventory* inventory;
	GError*                error;
	bool                   loading;
	gint                   revision;
	GCancellable*          cancellable;
} scan_update;
static gboolean apply_update(gpointer data) {
	scan_update* update = data;
	SignDevices* self = update->self;
	if (!self->closing && !g_cancellable_is_cancelled(update->cancellable) &&
	    update->revision == g_atomic_int_get(&self->revision)) {
		set_busy(self, update->loading);
		if (update->loading) {
			gtk_label_set_text(self->status, _("Scanning devices…"));
		} else if (update->inventory) {
			render(self, update->inventory);
		} else if (update->error) {
			clear_content(self);
			char* text = gui_error_message(update->error, GUI_ERROR_DEVICES);
			gtk_label_set_text(self->status, text);
			g_free(text);
		}
	}
	return G_SOURCE_REMOVE;
}
static void update_free(gpointer data) {
	scan_update* update = data;
	sign_device_inventory_free(update->inventory);
	g_clear_error(&update->error);
	g_object_unref(update->self);
	g_object_unref(update->cancellable);
	g_free(update);
}
static void post_update(
    SignDevices*           self,
    GCancellable*          cancellable,
    gint                   revision,
    bool                   loading,
    sign_device_inventory* inventory,
    GError*                error
) {
	scan_update* update = g_new0(scan_update, 1);
	update->self = g_object_ref(self);
	update->cancellable = g_object_ref(cancellable);
	update->loading = loading;
	update->revision = revision;
	update->inventory = inventory;
	update->error = error;
	GSource* source = g_idle_source_new();
	g_source_set_callback(source, apply_update, update, update_free);
	g_source_attach(source, g_main_context_default());
	g_source_unref(source);
}
static void monitor_worker(
    GTask*        task,
    gpointer      source,
    gpointer      data,
    GCancellable* cancellable
) {
	(void)data;
	SignDevices*         self = source;
	libssig_context      context = {0};
	libssig_device_state state = {0};
	sign_args            settings = {0};
	unsigned             retry = 0;
	gint                 last_revision = -1;
	while (!g_cancellable_is_cancelled(cancellable)) {
		gint revision = g_atomic_int_get(&self->revision);
		bool reload = revision != last_revision;
		last_revision = revision;
		bool    changed = reload;
		GError* error = NULL;
		if (reload || (!context.impl && ++retry >= 5)) {
			retry = 0;
			changed = true;
			post_update(self, cancellable, revision, true, NULL, NULL);
			libssig_context_close(&context);
			arg_free(&settings);
			state = (libssig_device_state){0};
			char detail[512];
			if (!config_load(&settings, NULL, detail, sizeof(detail))) {
				g_set_error_literal(
				    &error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    detail
				);
			} else {
				libssig_status status = libssig_context_open_modules(
				    &context,
				    &settings.options.modules
				);
				if (status != LIBSSIG_OK) {
					g_set_error_literal(
					    &error,
					    SIGN_GUI_ERROR,
					    G_IO_ERROR_FAILED,
					    _("Cannot load the PKCS#11 modules")
					);
				}
			}
		}
		if (context.impl) {
			bool           hardware_changed = false;
			libssig_status status = libssig_devices_changed(
			    &context,
			    &state,
			    &hardware_changed,
			    g_atomic_int_get(&self->include_software) ? LIBSSIG_DEVICES_ALL
			                                              : 0
			);
			changed |= hardware_changed;
			if (status != LIBSSIG_OK) {
				changed = true;
				g_set_error_literal(
				    &error,
				    SIGN_GUI_ERROR,
				    G_IO_ERROR_FAILED,
				    _("Cannot monitor devices")
				);
			}
		}
		if (changed) {
			post_update(self, cancellable, revision, true, NULL, NULL);
			sign_device_inventory*
			    inventory = context.impl && !error
			                    ? sign_device_inventory_scan(
			                          &context,
			                          &settings.options,
			                          g_atomic_int_get(&self->include_software)
			                              ? LIBSSIG_DEVICES_ALL
			                              : 0,
			                          cancellable,
			                          &error
			                      )
			                    : NULL;
			post_update(self, cancellable, revision, false, inventory, error);
		} else {
			g_clear_error(&error);
		}
		for (unsigned n = 0;
		     n < 10 && !g_cancellable_is_cancelled(cancellable) &&
		     g_atomic_int_get(&self->revision) == last_revision;
		     ++n) {
			g_usleep(100000);
		}
	}
	libssig_context_close(&context);
	arg_free(&settings);
	g_task_return_boolean(task, TRUE);
}
static void monitor_finished(
    GObject*      source,
    GAsyncResult* result,
    gpointer      data
) {
	(void)data;
	SignDevices* self = SIGN_DEVICES(source);
	g_task_propagate_boolean(G_TASK(result), NULL);
	self->monitoring = false;
	if (!self->closing) {
		if (g_cancellable_is_cancelled(self->cancellable)) {
			sign_devices_refresh(self);
		} else {
			set_busy(self, false);
		}
	}
}
bool sign_devices_is_busy(SignDevices* self) {
	return self->busy;
}
void sign_devices_refresh(SignDevices* self) {
	if (self->closing) {
		return;
	}
	g_atomic_int_inc(&self->revision);
	set_busy(self, true);
	if (self->monitoring) {
		return;
	}
	if (self->cancellable) {
		g_object_unref(self->cancellable);
	}
	self->cancellable = g_cancellable_new();
	self->monitoring = true;
	GTask* task = g_task_new(self, self->cancellable, monitor_finished, NULL);
	g_task_set_return_on_cancel(task, FALSE);
	g_task_run_in_thread(task, monitor_worker);
	g_object_unref(task);
}
static void refresh_clicked(GtkButton* button, gpointer data) {
	(void)button;
	sign_devices_refresh(SIGN_DEVICES(data));
}
static void hardware_toggled(GtkCheckButton* button, gpointer data) {
	SignDevices* self = data;
	g_atomic_int_set(
	    &self->include_software,
	    !gtk_check_button_get_active(button)
	);
	sign_devices_refresh(self);
}
static void devices_unroot(GtkWidget* widget) {
	SignDevices* self = SIGN_DEVICES(widget);
	self->closing = true;
	if (self->cancellable) {
		g_cancellable_cancel(self->cancellable);
	}
	GTK_WIDGET_CLASS(sign_devices_parent_class)->unroot(widget);
}
static void devices_root(GtkWidget* widget) {
	GTK_WIDGET_CLASS(sign_devices_parent_class)->root(widget);
	SIGN_DEVICES(widget)->closing = false;
}
static void devices_dispose(GObject* object) {
	SignDevices* self = SIGN_DEVICES(object);
	if (self->cancellable) {
		g_cancellable_cancel(self->cancellable);
		g_object_unref(self->cancellable);
		self->cancellable = NULL;
	}
	if (self->header_menu) {
		g_object_unref(self->header_menu);
		self->header_menu = NULL;
	}
	G_OBJECT_CLASS(sign_devices_parent_class)->dispose(object);
}
static void devices_get_property(
    GObject*    object,
    guint       property,
    GValue*     value,
    GParamSpec* pspec
) {
	if (property == PROP_BUSY) {
		g_value_set_boolean(value, SIGN_DEVICES(object)->busy);
	} else {
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property, pspec);
	}
}
static void sign_devices_class_init(SignDevicesClass* klass) {
	GTK_WIDGET_CLASS(klass)->root = devices_root;
	GTK_WIDGET_CLASS(klass)->unroot = devices_unroot;
	G_OBJECT_CLASS(klass)->dispose = devices_dispose;
	G_OBJECT_CLASS(klass)->get_property = devices_get_property;
	properties[PROP_BUSY] = g_param_spec_boolean(
	    "busy",
	    NULL,
	    NULL,
	    FALSE,
	    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
	);
	g_object_class_install_properties(
	    G_OBJECT_CLASS(klass),
	    N_PROPERTIES,
	    properties
	);
}
static void sign_devices_init(SignDevices* self) {
	gtk_orientable_set_orientation(
	    GTK_ORIENTABLE(self),
	    GTK_ORIENTATION_VERTICAL
	);
	gtk_box_set_spacing(GTK_BOX(self), 12);
	gtk_widget_set_margin_start(GTK_WIDGET(self), 18);
	gtk_widget_set_margin_end(GTK_WIDGET(self), 18);
	gtk_widget_set_margin_top(GTK_WIDGET(self), 12);
	gtk_widget_set_margin_bottom(GTK_WIDGET(self), 12);
	self->header_menu = g_object_ref_sink(gtk_menu_button_new());
	gtk_widget_set_name(self->header_menu, "devices-options");
	gtk_menu_button_set_icon_name(
	    GTK_MENU_BUTTON(self->header_menu),
	    "view-more-symbolic"
	);
	gtk_widget_add_css_class(self->header_menu, "flat");
	gtk_widget_set_tooltip_text(self->header_menu, _("Device options"));
	GtkWidget* popover = gtk_popover_new();
	GtkWidget* hardware = gtk_check_button_new_with_label(_("Hardware only"));
	gtk_widget_set_name(hardware, "devices-hardware-only");
	gtk_check_button_set_active(GTK_CHECK_BUTTON(hardware), TRUE);
	gtk_widget_set_margin_start(hardware, 12);
	gtk_widget_set_margin_end(hardware, 12);
	gtk_widget_set_margin_top(hardware, 12);
	gtk_widget_set_margin_bottom(hardware, 12);
	g_signal_connect_object(
	    hardware,
	    "toggled",
	    G_CALLBACK(hardware_toggled),
	    self,
	    0
	);
	gtk_popover_set_child(GTK_POPOVER(popover), hardware);
	gtk_menu_button_set_popover(GTK_MENU_BUTTON(self->header_menu), popover);
	GtkWidget* scroll = gtk_scrolled_window_new();
	gtk_widget_set_vexpand(scroll, TRUE);
	gtk_scrolled_window_set_policy(
	    GTK_SCROLLED_WINDOW(scroll),
	    GTK_POLICY_NEVER,
	    GTK_POLICY_AUTOMATIC
	);
	self->body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_name(self->body, "devices-body");
	gtk_widget_set_valign(self->body, GTK_ALIGN_START);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), self->body);
	self->content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_name(self->content, "devices-content");
	gtk_box_append(GTK_BOX(self->body), self->content);
	GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_widget_set_name(bar, "devices-controls");
	gtk_box_append(GTK_BOX(self), bar);
	self->status = GTK_LABEL(gtk_label_new(_("Scanning devices…")));
	gtk_widget_set_name(GTK_WIDGET(self->status), "devices-status");
	gtk_label_set_wrap(self->status, TRUE);
	gtk_label_set_justify(self->status, GTK_JUSTIFY_LEFT);
	gtk_label_set_xalign(self->status, 0);
	gtk_widget_set_hexpand(GTK_WIDGET(self->status), TRUE);
	gtk_widget_add_css_class(GTK_WIDGET(self->status), "dim-label");
	gtk_box_append(GTK_BOX(bar), GTK_WIDGET(self->status));
	self->spinner = gtk_spinner_new();
	gtk_widget_set_halign(self->spinner, GTK_ALIGN_CENTER);
	gtk_widget_set_visible(self->spinner, FALSE);
	gtk_box_append(GTK_BOX(bar), self->spinner);
	self->refresh = gtk_button_new_with_label(_("Refresh"));
	gtk_widget_set_name(self->refresh, "devices-refresh");
	gtk_widget_set_halign(self->refresh, GTK_ALIGN_END);
	g_signal_connect(
	    self->refresh,
	    "clicked",
	    G_CALLBACK(refresh_clicked),
	    self
	);
	gtk_box_append(GTK_BOX(bar), self->refresh);
	gtk_box_append(GTK_BOX(self), scroll);
}
GtkWidget* sign_devices_new(void) {
	return g_object_new(SIGN_TYPE_DEVICES, NULL);
}

GtkWidget* sign_devices_header_menu(SignDevices* self) {
	return self->header_menu;
}
