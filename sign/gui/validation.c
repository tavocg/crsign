/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <jansson.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <sign/config.h>
#include <sign/gui/i18n.h>
#include <sign/gui/validation.h>
#include <stdlib.h>
#include <string.h>

typedef enum { QUEUED, RUNNING, FINISHED } item_state;
typedef struct {
	GFile *          file, *content;
	AdwExpanderRow*  row;
	GtkWidget *      details, *original, *icon, *spinner;
	item_state       state;
	sign_flow_result result;
	GError*          error;
	bool             ok;
} validation_item;
struct _SignValidation {
	GtkBox           parent_instance;
	GtkWidget *      drop_area, *scroll, *controls, *clear;
	GtkListBox*      list;
	GtkLabel*        summary;
	GtkLabel*        notice;
	GPtrArray*       items;
	GQueue           queue;
	validation_item* active;
	GCancellable*    cancellable;
	guint            retry, dialogs;
	bool             closing;
};
G_DEFINE_TYPE(SignValidation, sign_validation, GTK_TYPE_BOX)

static void start_next(SignValidation* self);
static void choose_files(GtkButton* button, gpointer data);
static void choose_original(GtkButton* button, gpointer data);
static void item_free(gpointer data) {
	validation_item* item = data;
	g_object_unref(item->file);
	if (item->content) {
		g_object_unref(item->content);
	}
	sign_flow_result_clear(&item->result);
	g_clear_error(&item->error);
	g_free(item);
}
bool sign_validation_is_busy(SignValidation* self) {
	return self->active || !g_queue_is_empty(&self->queue) || self->retry;
}
size_t sign_validation_count(SignValidation* self) {
	return self->items->len;
}
const sign_flow_document_result* sign_validation_result(
    SignValidation* self,
    size_t          index
) {
	if (index >= self->items->len) {
		return NULL;
	}
	validation_item* item = g_ptr_array_index(self->items, index);
	return item->state == FINISHED && item->result.count
	           ? &item->result.documents[0]
	           : NULL;
}
static void summary_update(SignValidation* self) {
	guint valid = 0, failed = 0, pending = 0;
	for (guint i = 0; i < self->items->len; ++i) {
		validation_item* item = g_ptr_array_index(self->items, i);
		if (item->state != FINISHED) {
			++pending;
		} else if (item->ok) {
			++valid;
		} else {
			++failed;
		}
	}
	char* text = g_strdup_printf(
	    _("Valid: %u · Failed: %u · Pending: %u"),
	    valid,
	    failed,
	    pending
	);
	gtk_label_set_text(self->summary, text);
	g_free(text);
	bool has_items = self->items->len != 0;
	gtk_widget_set_visible(self->controls, has_items);
	gtk_widget_set_visible(self->scroll, has_items);
	gtk_widget_set_vexpand(self->drop_area, !has_items);
	gtk_widget_set_sensitive(
	    self->clear,
	    !sign_validation_is_busy(self) && !self->dialogs
	);
}
static void detail(
    validation_item* item,
    const char*      title,
    const char*      value
) {
	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_set_margin_start(box, 12);
	gtk_widget_set_margin_end(box, 12);
	gtk_widget_set_margin_top(box, 8);
	gtk_widget_set_margin_bottom(box, 8);
	GtkWidget* key = gtk_label_new(title);
	gtk_label_set_xalign(GTK_LABEL(key), 0);
	gtk_widget_add_css_class(key, "dim-label");
	GtkWidget* label = gtk_label_new(value ? value : _("Not available"));
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_label_set_wrap(GTK_LABEL(label), TRUE);
	gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
	gtk_label_set_selectable(GTK_LABEL(label), TRUE);
	gtk_label_set_width_chars(GTK_LABEL(label), 1);
	gtk_box_append(GTK_BOX(box), key);
	gtk_box_append(GTK_BOX(box), label);
	gtk_box_append(GTK_BOX(item->details), box);
}
static const char* yes_no(bool value) {
	return value ? _("Yes") : _("No");
}
static const char* outcome(validation_item* item) {
	if (item->ok) {
		return _("Valid");
	}
	if (!item->result.count) {
		return _("Could not validate");
	}
	switch (item->result.documents[0].verification_status) {
	case LIBSSIG_ERROR_TRUST:
		return _("Untrusted or expired certificate");
	case LIBSSIG_ERROR_CONTENT_REQUIRED:
		return _("Original content required");
	case LIBSSIG_ERROR_REVOKED:
		return _("Revoked certificate");
	case LIBSSIG_ERROR_TIMESTAMP:
		return _("Invalid timestamp");
	case LIBSSIG_ERROR_EVIDENCE:
		return _("Incomplete validation evidence");
	case LIBSSIG_ERROR_OCSP:
		return _("Revocation check failed");
	case LIBSSIG_ERROR_FORMAT:
	case LIBSSIG_ERROR_UNSUPPORTED:
		return _("Unsupported or unsigned file");
	default:
		return _("Validation failed");
	}
}
static char* certificate_name(X509_NAME* name) {
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
static char* certificate_time(const ASN1_TIME* value) {
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
static void certificate_details(
    validation_item*             item,
    const libssig_verify_signer* signer
) {
	if (!signer->cert_der || !signer->cert_der_len) {
		return;
	}
	const unsigned char* bytes = signer->cert_der;
	X509* cert = d2i_X509(NULL, &bytes, (long)signer->cert_der_len);
	if (!cert) {
		return;
	}
	char* subject = certificate_name(X509_get_subject_name(cert));
	char* issuer = certificate_name(X509_get_issuer_name(cert));
	char* from = certificate_time(X509_get0_notBefore(cert));
	char* until = certificate_time(X509_get0_notAfter(cert));
	detail(item, _("Certificate subject"), subject);
	detail(item, _("Certificate issuer"), issuer);
	detail(item, _("Valid from"), from);
	detail(item, _("Valid until"), until);
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int  length;
	if (X509_digest(cert, EVP_sha256(), digest, &length) == 1) {
		GString* fingerprint = g_string_new(NULL);
		for (unsigned int i = 0; i < length; ++i) {
			g_string_append_printf(
			    fingerprint,
			    i ? ":%02X" : "%02X",
			    digest[i]
			);
		}
		detail(item, _("Certificate SHA-256"), fingerprint->str);
		g_string_free(fingerprint, TRUE);
	}
	g_free(subject);
	g_free(issuer);
	g_free(from);
	g_free(until);
	X509_free(cert);
}
static void timestamp_details(
    validation_item*                item,
    const char*                     title,
    const libssig_verify_timestamp* timestamp
) {
	if (!timestamp->present) {
		return;
	}
	detail(
	    item,
	    title,
	    timestamp->time[0] ? timestamp->time : _("Time not authenticated")
	);
	detail(item, _("Timestamp signature valid"), yes_no(timestamp->valid));
	detail(
	    item,
	    _("Timestamp certificate trusted"),
	    timestamp->trust_checked ? yes_no(timestamp->trusted) : _("Not checked")
	);
}
static void render(validation_item* item) {
	GtkWidget* child;
	while ((child = gtk_widget_get_first_child(item->details))) {
		gtk_box_remove(GTK_BOX(item->details), child);
	}
	char* path = g_file_get_parse_name(item->file);
	detail(item, _("File"), path);
	g_free(path);
	if (item->content) {
		path = g_file_get_parse_name(item->content);
		detail(item, _("Original content"), path);
		g_free(path);
	}
	gtk_spinner_stop(GTK_SPINNER(item->spinner));
	gtk_widget_set_visible(item->spinner, FALSE);
	gtk_widget_set_visible(item->icon, TRUE);
	gtk_image_set_from_icon_name(
	    GTK_IMAGE(item->icon),
	    item->ok ? "object-select-symbolic" : "dialog-warning-symbolic"
	);
	gtk_widget_remove_css_class(item->icon, "success");
	gtk_widget_remove_css_class(item->icon, "warning");
	gtk_widget_add_css_class(item->icon, item->ok ? "success" : "warning");
	adw_expander_row_set_subtitle(item->row, outcome(item));
	bool needs_content = item->result.count &&
	                     item->result.documents[0].verification_status ==
	                         LIBSSIG_ERROR_CONTENT_REQUIRED;
	gtk_widget_set_visible(item->original, needs_content);
	if (item->error &&
	    (!item->result.count || item->result.documents[0].verification_status ==
	                                LIBSSIG_ERROR_ARGUMENT)) {
		const char* message = item->error->message;
		const char* prefix = "documents[0]: ";
		if (g_str_has_prefix(message, prefix)) {
			message += strlen(prefix);
		}
		detail(item, _("Details"), message);
	}
	if (!item->result.count) {
		return;
	}
	const libssig_verify_result* result = &item->result.documents[0]
	                                           .verification;
	if (!result->count) {
		return;
	}
	const char* formats[] = {"CAdES", "CMS", "PAdES", "XAdES", "JAdES"};
	detail(
	    item,
	    _("Format"),
	    (unsigned int)result->format <= LIBSSIG_FORMAT_JADES
	        ? formats[result->format]
	        : _("Unknown")
	);
	detail(item, _("Signature valid"), yes_no(result->signature_valid));
	detail(
	    item,
	    _("Certificate chain trusted"),
	    result->trust_checked ? yes_no(result->trusted) : _("Not checked")
	);
	detail(item, _("Detached signature"), yes_no(result->detached));
	for (size_t i = 0; i < result->count; ++i) {
		const libssig_verify_signer* signer = &result->signers[i];
		char*      title = g_strdup_printf(_("Signer %zu"), i + 1);
		GtkWidget* heading = gtk_label_new(title);
		gtk_label_set_xalign(GTK_LABEL(heading), 0);
		gtk_widget_add_css_class(heading, "heading");
		gtk_widget_set_margin_start(heading, 12);
		gtk_widget_set_margin_top(heading, 12);
		gtk_box_append(GTK_BOX(item->details), heading);
		detail(
		    item,
		    _("Detected signature level"),
		    signer->level_detected
		        ? sign_option_choice(SIGN_OPTION_LEVEL, signer->level)
		        : _("No baseline level detected")
		);
		g_free(title);
		detail(
		    item,
		    _("Signature level validated"),
		    yes_no(signer->level_validated)
		);
		certificate_details(item, signer);
		if (signer->trust_error) {
			detail(
			    item,
			    _("Certificate trust error"),
			    X509_verify_cert_error_string(signer->trust_error)
			);
		}
		detail(
		    item,
		    _("Revocation checked"),
		    yes_no(signer->revocation_checked)
		);
		timestamp_details(item, _("Signing timestamp"), &signer->timestamp);
		timestamp_details(item, _("Archive timestamp"), &signer->archive);
	}
}
/* The GUI builds a JSON flow; trust paths are already resolved by config_load.
 * No profile commands, PINs or PKCS#11 modules enter verification flows. */
static sign_flow* verification_flow(validation_item* item, GError** error) {
	char* input = g_file_get_path(item->file);
	char* content = item->content ? g_file_get_path(item->content) : NULL;
	if (!input || (item->content && !content)) {
		g_set_error_literal(
		    error,
		    G_IO_ERROR,
		    G_IO_ERROR_NOT_SUPPORTED,
		    _("Only local files can be validated")
		);
		g_free(input);
		g_free(content);
		return NULL;
	}
	sign_args settings = {0};
	char      detail[512];
	if (!config_load(&settings, NULL, detail, sizeof(detail))) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, detail);
		g_free(input);
		g_free(content);
		return NULL;
	}
	json_t* document = json_pack(
	    "{s:s,s:s}",
	    "operation",
	    "verify",
	    "input",
	    input
	);
	if (content) {
		json_object_set_new(document, "content", json_string(content));
	}
	const sign_options* options = &settings.options;
	if (options->ca_file) {
		json_object_set_new(document, "ca", json_string(options->ca_file));
	}
	if (options->intermediates) {
		json_object_set_new(
		    document,
		    "intermediates",
		    json_string(options->intermediates)
		);
	}
	json_object_set_new(
	    document,
	    "no_bundled_certs",
	    json_boolean(options->no_bundled_certs)
	);
	json_t* bundles = json_array();
	for (size_t i = 0; i < options->cert_bundle_count; ++i) {
		json_array_append_new(bundles, json_string(options->cert_bundles[i]));
	}
	json_object_set_new(document, "cert_bundles", bundles);
	json_t* tsa = json_object();
	if (options->tsa_ca_file) {
		json_object_set_new(tsa, "ca", json_string(options->tsa_ca_file));
	}
	if (options->tsa_intermediates) {
		json_object_set_new(
		    tsa,
		    "intermediates",
		    json_string(options->tsa_intermediates)
		);
	}
	json_object_set_new(document, "tsa", tsa);
	json_t* documents = json_array();
	json_array_append_new(documents, document);
	json_t* root = json_pack(
	    "{s:i,s:o}",
	    "version",
	    SIGN_FLOW_VERSION,
	    "documents",
	    documents
	);
	char* text = json_dumps(root, JSON_COMPACT);
	sign_flow*
	    flow = text ? sign_flow_parse_json(text, strlen(text), NULL, error)
	                : NULL;
	if (!text) {
		g_set_error_literal(
		    error,
		    G_IO_ERROR,
		    G_IO_ERROR_NO_SPACE,
		    _("Cannot create validation flow")
		);
	}
	free(text);
	json_decref(root);
	arg_free(&settings);
	g_free(input);
	g_free(content);
	return flow;
}
static void worker(
    GTask*        task,
    gpointer      source,
    gpointer      data,
    GCancellable* cancellable
) {
	(void)source;
	validation_item* item = data;
	if (g_cancellable_set_error_if_cancelled(cancellable, &item->error)) {
		g_task_return_boolean(task, TRUE);
		return;
	}
	sign_flow* flow = verification_flow(item, &item->error);
	if (flow) {
		item->ok = sign_flow_run(flow, NULL, &item->result, &item->error);
		sign_flow_free(flow);
	}
	g_task_return_boolean(task, TRUE);
}
static gboolean retry_later(gpointer data) {
	SignValidation* self = data;
	self->retry = 0;
	start_next(self);
	return G_SOURCE_REMOVE;
}
static void finished(GObject* source, GAsyncResult* result, gpointer data) {
	(void)data;
	SignValidation*  self = SIGN_VALIDATION(source);
	GTask*           task = G_TASK(result);
	validation_item* item = g_task_get_task_data(task);
	g_task_propagate_boolean(task, NULL);
	self->active = NULL;
	if (self->closing) {
		sign_flow_result_clear(&item->result);
		g_clear_error(&item->error);
		item->ok = false;
		item->state = QUEUED;
		g_queue_push_head(&self->queue, item);
		return;
	}
	bool busy = g_error_matches(
	    item->error,
	    SIGN_FLOW_ERROR,
	    SIGN_FLOW_ERROR_BUSY
	);
	bool cancelled = g_error_matches(
	    item->error,
	    G_IO_ERROR,
	    G_IO_ERROR_CANCELLED
	);
	if (busy || cancelled) {
		sign_flow_result_clear(&item->result);
		g_clear_error(&item->error);
		item->state = QUEUED;
		adw_expander_row_set_subtitle(
		    item->row,
		    _("Waiting for another operation")
		);
		g_queue_push_head(&self->queue, item);
		if (busy) {
			self->retry = g_timeout_add(100, retry_later, self);
		} else {
			start_next(self);
		}
	} else {
		item->state = FINISHED;
		render(item);
		start_next(self);
	}
	summary_update(self);
}
static void start_next(SignValidation* self) {
	if (self->closing || self->active || self->retry ||
	    g_queue_is_empty(&self->queue)) {
		return;
	}
	validation_item* item = g_queue_pop_head(&self->queue);
	self->active = item;
	item->state = RUNNING;
	gtk_widget_set_visible(item->icon, FALSE);
	gtk_widget_set_visible(item->spinner, TRUE);
	gtk_spinner_start(GTK_SPINNER(item->spinner));
	adw_expander_row_set_subtitle(item->row, _("Validating…"));
	GTask* task = g_task_new(self, self->cancellable, finished, NULL);
	g_task_set_check_cancellable(task, FALSE);
	g_task_set_task_data(task, item, NULL);
	g_task_run_in_thread(task, worker);
	g_object_unref(task);
}
void sign_validation_set_content(
    SignValidation* self,
    size_t          index,
    GFile*          content
) {
	if (self->closing || index >= self->items->len || !G_IS_FILE(content)) {
		return;
	}
	validation_item* item = g_ptr_array_index(self->items, index);
	if (item->state != FINISHED) {
		return;
	}
	if (item->content) {
		g_object_unref(item->content);
	}
	item->content = g_object_ref(content);
	sign_flow_result_clear(&item->result);
	g_clear_error(&item->error);
	item->ok = false;
	item->state = QUEUED;
	gtk_widget_set_visible(item->original, FALSE);
	g_queue_push_tail(&self->queue, item);
	adw_expander_row_set_subtitle(item->row, _("Queued"));
	start_next(self);
	summary_update(self);
}
void sign_validation_add_files(SignValidation* self, GListModel* files) {
	if (self->closing) {
		return;
	}
	gtk_widget_set_visible(GTK_WIDGET(self->notice), FALSE);
	for (guint i = 0; i < g_list_model_get_n_items(files); ++i) {
		GFile* file = g_list_model_get_item(files, i);
		if (!G_IS_FILE(file)) {
			if (file) {
				g_object_unref(file);
			}
			continue;
		}
		validation_item* item = g_new0(validation_item, 1);
		item->file = file;
		item->row = ADW_EXPANDER_ROW(adw_expander_row_new());
		char* basename = g_file_get_basename(file);
		char* display = basename ? g_filename_display_name(basename)
		                         : g_file_get_parse_name(file);
		adw_preferences_row_set_use_markup(
		    ADW_PREFERENCES_ROW(item->row),
		    FALSE
		);
		adw_preferences_row_set_title(ADW_PREFERENCES_ROW(item->row), display);
		g_free(display);
		g_free(basename);
		adw_expander_row_set_subtitle(item->row, _("Queued"));
		item->spinner = gtk_spinner_new();
		gtk_widget_set_valign(item->spinner, GTK_ALIGN_CENTER);
		adw_expander_row_add_prefix(item->row, item->spinner);
		item->icon = gtk_image_new();
		adw_expander_row_add_prefix(item->row, item->icon);
		item->details = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		adw_expander_row_add_row(item->row, item->details);
		item->original = gtk_button_new_with_label(
		    _("Select original content…")
		);
		gtk_widget_set_halign(item->original, GTK_ALIGN_START);
		gtk_widget_set_margin_start(item->original, 12);
		gtk_widget_set_margin_bottom(item->original, 12);
		adw_expander_row_add_row(item->row, item->original);
		gtk_widget_set_visible(item->original, FALSE);
		g_object_set_data(G_OBJECT(item->original), "validation-item", item);
		g_signal_connect(
		    item->original,
		    "clicked",
		    G_CALLBACK(choose_original),
		    self
		);
		gtk_list_box_append(self->list, GTK_WIDGET(item->row));
		g_ptr_array_add(self->items, item);
		g_queue_push_tail(&self->queue, item);
	}
	start_next(self);
	summary_update(self);
}
typedef struct {
	SignValidation*  self;
	validation_item* item;
} file_request;
static GtkWindow* parent_window(SignValidation* self) {
	GtkRoot* root = gtk_widget_get_root(GTK_WIDGET(self));
	return GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : NULL;
}
static void dialog_error(SignValidation* self, const GError* error) {
	if (self->closing || !error ||
	    g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED) ||
	    g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_CANCELLED) ||
	    g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		return;
	}
	gtk_label_set_text(self->notice, error->message);
	gtk_widget_set_visible(GTK_WIDGET(self->notice), TRUE);
}
static void selected_files(
    GObject*      source,
    GAsyncResult* result,
    gpointer      data
) {
	SignValidation* self = data;
	GError*         error = NULL;
	GListModel*     files = gtk_file_dialog_open_multiple_finish(
        GTK_FILE_DIALOG(source),
        result,
        &error
    );
	--self->dialogs;
	if (files) {
		sign_validation_add_files(self, files);
		g_object_unref(files);
	}
	dialog_error(self, error);
	g_clear_error(&error);
	if (!self->closing) {
		summary_update(self);
	}
	g_object_unref(self);
}
static void choose_files(GtkButton* button, gpointer data) {
	(void)button;
	SignValidation* self = data;
	GtkFileDialog*  dialog = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dialog, _("Select signed files"));
	++self->dialogs;
	summary_update(self);
	gtk_file_dialog_open_multiple(
	    dialog,
	    parent_window(self),
	    self->cancellable,
	    selected_files,
	    g_object_ref(self)
	);
	g_object_unref(dialog);
}
static void selected_content(
    GObject*      source,
    GAsyncResult* result,
    gpointer      data
) {
	file_request*   request = data;
	SignValidation* self = request->self;
	GError*         error = NULL;
	GFile*          file = gtk_file_dialog_open_finish(
        GTK_FILE_DIALOG(source),
        result,
        &error
    );
	--self->dialogs;
	if (file && !self->closing) {
		for (guint i = 0; i < self->items->len; ++i) {
			if (g_ptr_array_index(self->items, i) == request->item) {
				sign_validation_set_content(self, i, file);
				break;
			}
		}
	}
	if (file) {
		g_object_unref(file);
	}
	dialog_error(self, error);
	g_clear_error(&error);
	if (!self->closing) {
		summary_update(self);
	}
	g_object_unref(self);
	g_free(request);
}
static void choose_original(GtkButton* button, gpointer data) {
	SignValidation* self = data;
	file_request*   request = g_new0(file_request, 1);
	request->self = g_object_ref(self);
	request->item = g_object_get_data(G_OBJECT(button), "validation-item");
	GtkFileDialog* dialog = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dialog, _("Select original content"));
	++self->dialogs;
	summary_update(self);
	gtk_file_dialog_open(
	    dialog,
	    parent_window(self),
	    self->cancellable,
	    selected_content,
	    request
	);
	g_object_unref(dialog);
}
/* Prefer the explicit URI list offered by file managers. This avoids routing
 * a native, unsandboxed file drop through an optional FileTransfer portal.
 * Keep the typed target below for platforms/providers without URI lists. */
typedef struct {
	SignValidation* self;
	GdkDrop*        drop;
	GInputStream*   stream;
} drop_request;
static gboolean typed_drop_accept(
    GtkDropTarget* target,
    GdkDrop*       drop,
    gpointer       data
) {
	(void)data;
	GdkContentFormats* formats = gdk_drop_get_formats(drop);
	return !gdk_content_formats_contain_mime_type(formats, "text/uri-list") &&
	       (gdk_drop_get_actions(drop) & GDK_ACTION_COPY) &&
	       gdk_content_formats_match_gtype(
	           gtk_drop_target_get_formats(target),
	           formats
	       ) != G_TYPE_INVALID;
}
static void drop_complete(
    drop_request* request,
    const GValue* value,
    GError*       error
) {
	SignValidation* self = request->self;
	bool            accepted = false;
	if (!self->closing && value && G_VALUE_HOLDS(value, GDK_TYPE_FILE_LIST)) {
		GdkFileList* list = g_value_get_boxed(value);
		GSList*      files = list ? gdk_file_list_get_files(list) : NULL;
		if (files) {
			GListStore* model = g_list_store_new(G_TYPE_FILE);
			for (GSList* node = files; node; node = node->next) {
				g_list_store_append(model, node->data);
			}
			sign_validation_add_files(self, G_LIST_MODEL(model));
			g_object_unref(model);
			accepted = true;
		} else if (!error) {
			error = g_error_new_literal(
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    _("No files were received from the file manager")
			);
		}
	}
	/* Always negotiate COPY, including when the source initially prefers MOVE.
	 * Validation never takes ownership of the source files. */
	gdk_drop_finish(request->drop, accepted ? GDK_ACTION_COPY : 0);
	--self->dialogs;
	dialog_error(self, error);
	g_clear_error(&error);
	if (request->stream) {
		g_object_unref(request->stream);
	}
	if (!self->closing) {
		summary_update(self);
	}
	g_object_unref(request->drop);
	g_object_unref(self);
	g_free(request);
}
static void uri_deserialized(
    GObject*      source,
    GAsyncResult* result,
    gpointer      data
) {
	(void)source;
	GError* error = NULL;
	GValue  value = G_VALUE_INIT;
	bool    ok = gdk_content_deserialize_finish(result, &value, &error);
	drop_complete(data, ok ? &value : NULL, error);
	if (G_VALUE_TYPE(&value)) {
		g_value_unset(&value);
	}
}
static void uri_received(GObject* source, GAsyncResult* result, gpointer data) {
	drop_request* request = data;
	GError*       error = NULL;
	const char*   mime = NULL;
	request->stream = gdk_drop_read_finish(
	    GDK_DROP(source),
	    result,
	    &mime,
	    &error
	);
	if (!request->stream) {
		drop_complete(request, NULL, error);
		return;
	}
	gdk_content_deserialize_async(
	    request->stream,
	    mime,
	    GDK_TYPE_FILE_LIST,
	    G_PRIORITY_DEFAULT,
	    request->self->cancellable,
	    uri_deserialized,
	    request
	);
}
static gboolean uri_accept(
    GtkDropTargetAsync* target,
    GdkDrop*            drop,
    gpointer            data
) {
	(void)target;
	return !SIGN_VALIDATION(data)->closing &&
	       gdk_content_formats_contain_mime_type(
	           gdk_drop_get_formats(drop),
	           "text/uri-list"
	       );
}
static GdkDragAction uri_motion(
    GtkDropTargetAsync* target,
    GdkDrop*            drop,
    double              x,
    double              y,
    gpointer            data
) {
	(void)target;
	(void)x;
	(void)y;
	/* get_actions() can reflect the currently selected action on Wayland.
	 * Advertise our own preference so the compositor can renegotiate it. */
	(void)drop;
	return SIGN_VALIDATION(data)->closing ? 0 : GDK_ACTION_COPY;
}
static gboolean uri_dropped(
    GtkDropTargetAsync* target,
    GdkDrop*            drop,
    double              x,
    double              y,
    gpointer            data
) {
	(void)target;
	(void)x;
	(void)y;
	SignValidation* self = data;
	if (self->closing) {
		return FALSE;
	}
	static const char* mime_types[] = {"text/uri-list", NULL};
	drop_request*      request = g_new0(drop_request, 1);
	request->self = g_object_ref(self);
	request->drop = g_object_ref(drop);
	++self->dialogs;
	summary_update(self);
	gdk_drop_read_async(
	    drop,
	    mime_types,
	    G_PRIORITY_DEFAULT,
	    self->cancellable,
	    uri_received,
	    request
	);
	return TRUE;
}
static gboolean dropped(
    GtkDropTarget* target,
    const GValue*  value,
    double         x,
    double         y,
    gpointer       data
) {
	(void)target;
	(void)x;
	(void)y;
	GdkFileList* list = g_value_get_boxed(value);
	if (!list) {
		return FALSE;
	}
	GListStore* files = g_list_store_new(G_TYPE_FILE);
	for (GSList* node = gdk_file_list_get_files(list); node;
	     node = node->next) {
		g_list_store_append(files, node->data);
	}
	sign_validation_add_files(SIGN_VALIDATION(data), G_LIST_MODEL(files));
	g_object_unref(files);
	return TRUE;
}
static void clear_results(GtkButton* button, gpointer data) {
	(void)button;
	SignValidation* self = data;
	if (sign_validation_is_busy(self) || self->dialogs) {
		return;
	}
	GtkWidget* row;
	while ((row = gtk_widget_get_first_child(GTK_WIDGET(self->list)))) {
		gtk_list_box_remove(self->list, row);
	}
	g_ptr_array_set_size(self->items, 0);
	summary_update(self);
}
static void sign_validation_root(GtkWidget* widget) {
	SignValidation* self = SIGN_VALIDATION(widget);
	GTK_WIDGET_CLASS(sign_validation_parent_class)->root(widget);
	/* Unroot is also emitted during reparenting, not just final destruction. */
	self->closing = false;
	if (g_cancellable_is_cancelled(self->cancellable)) {
		g_object_unref(self->cancellable);
		self->cancellable = g_cancellable_new();
	}
	start_next(self);
}
static void sign_validation_unroot(GtkWidget* widget) {
	SignValidation* self = SIGN_VALIDATION(widget);
	self->closing = true;
	g_cancellable_cancel(self->cancellable);
	if (self->retry) {
		g_source_remove(self->retry);
		self->retry = 0;
	}
	GTK_WIDGET_CLASS(sign_validation_parent_class)->unroot(widget);
}
static void sign_validation_dispose(GObject* object) {
	SignValidation* self = SIGN_VALIDATION(object);
	g_cancellable_cancel(self->cancellable);
	if (self->retry) {
		g_source_remove(self->retry);
		self->retry = 0;
	}
	G_OBJECT_CLASS(sign_validation_parent_class)->dispose(object);
}
static void sign_validation_finalize(GObject* object) {
	SignValidation* self = SIGN_VALIDATION(object);
	g_queue_clear(&self->queue);
	g_ptr_array_unref(self->items);
	g_object_unref(self->cancellable);
	G_OBJECT_CLASS(sign_validation_parent_class)->finalize(object);
}
static void sign_validation_class_init(SignValidationClass* klass) {
	GTK_WIDGET_CLASS(klass)->root = sign_validation_root;
	GTK_WIDGET_CLASS(klass)->unroot = sign_validation_unroot;
	G_OBJECT_CLASS(klass)->dispose = sign_validation_dispose;
	G_OBJECT_CLASS(klass)->finalize = sign_validation_finalize;
}
static void sign_validation_init(SignValidation* self) {
	GdkDisplay* display = gtk_widget_get_display(GTK_WIDGET(self));
	if (!g_object_get_data(G_OBJECT(display), "crsign-validation-style")) {
		GtkCssProvider* provider = gtk_css_provider_new();
		gtk_css_provider_load_from_resource(
		    provider,
		    "/org/crsign/Sign/style.css"
		);
		gtk_style_context_add_provider_for_display(
		    display,
		    GTK_STYLE_PROVIDER(provider),
		    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
		);
		g_object_set_data_full(
		    G_OBJECT(display),
		    "crsign-validation-style",
		    provider,
		    g_object_unref
		);
	}
	gtk_widget_add_css_class(GTK_WIDGET(self), "validation-page");
	gtk_orientable_set_orientation(
	    GTK_ORIENTABLE(self),
	    GTK_ORIENTATION_VERTICAL
	);
	gtk_box_set_spacing(GTK_BOX(self), 12);
	gtk_widget_set_margin_start(GTK_WIDGET(self), 18);
	gtk_widget_set_margin_end(GTK_WIDGET(self), 18);
	gtk_widget_set_margin_top(GTK_WIDGET(self), 18);
	gtk_widget_set_margin_bottom(GTK_WIDGET(self), 18);
	self->items = g_ptr_array_new_with_free_func(item_free);
	g_queue_init(&self->queue);
	self->cancellable = g_cancellable_new();
	self->drop_area = gtk_button_new();
	gtk_widget_set_name(self->drop_area, "validation-choose");
	gtk_widget_add_css_class(self->drop_area, "flat");
	gtk_widget_add_css_class(self->drop_area, "validation-drop-area");
	gtk_widget_set_cursor_from_name(self->drop_area, "pointer");
	gtk_widget_set_size_request(self->drop_area, -1, 150);
	GtkWidget* prompt = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_valign(prompt, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_top(prompt, 24);
	gtk_widget_set_margin_bottom(prompt, 24);
	GtkWidget* icon = gtk_image_new_from_icon_name("document-open-symbolic");
	gtk_image_set_pixel_size(GTK_IMAGE(icon), 48);
	gtk_box_append(GTK_BOX(prompt), icon);
	GtkWidget* title = gtk_label_new(_("Drop signed files here"));
	gtk_label_set_wrap(GTK_LABEL(title), TRUE);
	gtk_widget_add_css_class(title, "title-2");
	gtk_box_append(GTK_BOX(prompt), title);
	GtkWidget* hint = gtk_label_new(_("Click to choose one or more files"));
	gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
	gtk_label_set_justify(GTK_LABEL(hint), GTK_JUSTIFY_CENTER);
	gtk_widget_add_css_class(hint, "dim-label");
	gtk_box_append(GTK_BOX(prompt), hint);
	gtk_button_set_child(GTK_BUTTON(self->drop_area), prompt);
	gtk_box_append(GTK_BOX(self), self->drop_area);
	self->notice = GTK_LABEL(gtk_label_new(NULL));
	gtk_label_set_wrap(self->notice, TRUE);
	gtk_label_set_wrap_mode(self->notice, PANGO_WRAP_WORD_CHAR);
	gtk_widget_add_css_class(GTK_WIDGET(self->notice), "error");
	gtk_widget_set_visible(GTK_WIDGET(self->notice), FALSE);
	gtk_box_append(GTK_BOX(self), GTK_WIDGET(self->notice));
	g_signal_connect(
	    self->drop_area,
	    "clicked",
	    G_CALLBACK(choose_files),
	    self
	);
	self->controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	self->summary = GTK_LABEL(gtk_label_new(NULL));
	gtk_label_set_wrap(self->summary, TRUE);
	gtk_label_set_xalign(self->summary, 0);
	gtk_widget_set_hexpand(GTK_WIDGET(self->summary), TRUE);
	gtk_box_append(GTK_BOX(self->controls), GTK_WIDGET(self->summary));
	self->clear = gtk_button_new_with_label(_("Clear results"));
	gtk_widget_set_name(self->clear, "validation-clear");
	gtk_box_append(GTK_BOX(self->controls), self->clear);
	g_signal_connect(self->clear, "clicked", G_CALLBACK(clear_results), self);
	gtk_box_append(GTK_BOX(self), self->controls);
	self->scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(
	    GTK_SCROLLED_WINDOW(self->scroll),
	    GTK_POLICY_NEVER,
	    GTK_POLICY_AUTOMATIC
	);
	gtk_widget_set_vexpand(self->scroll, TRUE);
	self->list = GTK_LIST_BOX(gtk_list_box_new());
	gtk_widget_set_name(GTK_WIDGET(self->list), "validation-results");
	gtk_list_box_set_selection_mode(self->list, GTK_SELECTION_NONE);
	gtk_widget_add_css_class(GTK_WIDGET(self->list), "boxed-list");
	gtk_scrolled_window_set_child(
	    GTK_SCROLLED_WINDOW(self->scroll),
	    GTK_WIDGET(self->list)
	);
	gtk_box_append(GTK_BOX(self), self->scroll);
	GtkDropTarget* drop = gtk_drop_target_new(
	    GDK_TYPE_FILE_LIST,
	    GDK_ACTION_COPY
	);
	g_signal_connect(drop, "accept", G_CALLBACK(typed_drop_accept), self);
	g_signal_connect(drop, "drop", G_CALLBACK(dropped), self);
	gtk_widget_add_controller(GTK_WIDGET(self), GTK_EVENT_CONTROLLER(drop));
	const char*        mime_types[] = {"text/uri-list"};
	GdkContentFormats* formats = gdk_content_formats_new(
	    mime_types,
	    G_N_ELEMENTS(mime_types)
	);
	GtkDropTargetAsync* uri_target = gtk_drop_target_async_new(
	    formats,
	    GDK_ACTION_COPY
	);
	gtk_event_controller_set_propagation_phase(
	    GTK_EVENT_CONTROLLER(uri_target),
	    GTK_PHASE_CAPTURE
	);
	g_signal_connect(uri_target, "accept", G_CALLBACK(uri_accept), self);
	g_signal_connect(uri_target, "drag-enter", G_CALLBACK(uri_motion), self);
	g_signal_connect(uri_target, "drag-motion", G_CALLBACK(uri_motion), self);
	g_signal_connect(uri_target, "drop", G_CALLBACK(uri_dropped), self);
	gtk_widget_add_controller(
	    GTK_WIDGET(self),
	    GTK_EVENT_CONTROLLER(uri_target)
	);
	summary_update(self);
}
GtkWidget* sign_validation_new(void) {
	return g_object_new(SIGN_TYPE_VALIDATION, NULL);
}
