/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <sign/config.h>
#include <sign/file_state.h>
#include <sign/gui/certificate.h>
#include <sign/gui/details.h>
#include <sign/gui/dialog.h>
#include <sign/gui/error.h>
#include <sign/gui/file_actions.h>
#include <sign/gui/file_drop.h>
#include <sign/gui/i18n.h>
#include <sign/gui/validation.h>
#include <stdlib.h>
#include <string.h>

typedef enum { QUEUED, RUNNING, FINISHED } item_state;
typedef struct {
	GFile *          file, *content;
	GtkListBoxRow*   row;
	GtkLabel *       title, *status;
	GtkWidget *      details, *original, *icon, *spinner, *page, *remove;
	AdwDialog*       dialog;
	bool             details_opened, discarded;
	item_state       state;
	sign_flow_result result;
	sign_flow*       template;
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
	if (item->dialog) {
		g_object_unref(item->dialog);
	}
	g_object_unref(item->file);
	if (item->content) {
		g_object_unref(item->content);
	}
	sign_flow_result_clear(&item->result);
	sign_flow_free(item->template);
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
	gtk_widget_set_size_request(self->drop_area, -1, 150);
	gtk_widget_set_visible(self->controls, has_items);
	gtk_widget_set_visible(self->scroll, has_items);
	gtk_widget_set_vexpand(self->drop_area, !has_items);
	for (guint i = 0; i < self->items->len; ++i) {
		validation_item* item = g_ptr_array_index(self->items, i);
		gtk_widget_set_sensitive(
		    item->remove,
		    item->state != RUNNING && !self->dialogs
		);
	}
	gtk_widget_set_sensitive(
	    self->clear,
	    !sign_validation_is_busy(self) && !self->dialogs
	);
}
static GtkWidget* detail_list(void) {
	GtkWidget* list = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
	gtk_widget_add_css_class(list, "boxed-list");
	gtk_widget_set_valign(list, GTK_ALIGN_START);
	return list;
}
static void detail(GtkWidget* details, const char* title, const char* value) {
	sign_detail_field(
	    details,
	    title,
	    value && *value ? value : _("Not available")
	);
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
	case LIBSSIG_ERROR_VERIFY:
		return _("Invalid signature or altered content");
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
static void certificate_details(
    GtkWidget*                   details,
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
	char* subject = sign_certificate_name(X509_get_subject_name(cert));
	char* issuer = sign_certificate_name(X509_get_issuer_name(cert));
	char* from = sign_certificate_time(X509_get0_notBefore(cert));
	char* until = sign_certificate_time(X509_get0_notAfter(cert));
	detail(details, _("Certificate subject"), subject);
	detail(details, _("Certificate issuer"), issuer);
	detail(details, _("Valid from"), from);
	detail(details, _("Valid until"), until);
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
		detail(details, _("Certificate SHA-256"), fingerprint->str);
		g_string_free(fingerprint, TRUE);
	}
	g_free(subject);
	g_free(issuer);
	g_free(from);
	g_free(until);
	X509_free(cert);
}
static void timestamp_details(
    GtkWidget*                      details,
    const char*                     title,
    const libssig_verify_timestamp* timestamp
) {
	if (!timestamp->present) {
		return;
	}
	detail(
	    details,
	    title,
	    timestamp->time[0] ? timestamp->time : _("Time not authenticated")
	);
	detail(details, _("Timestamp signature valid"), yes_no(timestamp->valid));
	detail(
	    details,
	    _("Timestamp certificate trusted"),
	    timestamp->trust_checked ? yes_no(timestamp->trusted) : _("Not checked")
	);
}
static char* localized_time(const char* value) {
	GDateTime* utc = value && *value ? g_date_time_new_from_iso8601(value, NULL)
	                                 : NULL;
	GDateTime* local = utc ? g_date_time_to_local(utc) : NULL;
	char*      text = local ? g_date_time_format(local, "%x %X")
	                        : g_strdup(_("Not available"));
	if (local) {
		g_date_time_unref(local);
	}
	if (utc) {
		g_date_time_unref(utc);
	}
	return text;
}
static char* subtitle(validation_item* item) {
	const char*                  latest = NULL;
	const libssig_verify_result* result = item->result.count
	                                          ? &item->result.documents[0]
	                                                 .verification
	                                          : NULL;
	if (!item->ok || !result || !result->count) {
		return g_strdup(outcome(item));
	}
	for (size_t i = 0; i < result->count; ++i) {
		const libssig_verify_signer* signer = &result->signers[i];
		const char* time = signer->timestamp.valid && signer->timestamp.trusted
		                       ? signer->timestamp.time
		                       : signer->signing_time;
		if (*time && (!latest || strcmp(time, latest) > 0)) {
			latest = time;
		}
	}
	char* date = localized_time(latest);
	char* text = g_strdup_printf(_("%s · Signed: %s"), outcome(item), date);
	g_free(date);
	return text;
}
static void expand_signer(
    AdwExpanderRow* row,
    GParamSpec*     pspec,
    gpointer        data
) {
	(void)pspec;
	const libssig_verify_signer* signer = data;
	if (!adw_expander_row_get_expanded(row) ||
	    g_object_get_data(G_OBJECT(row), "loaded")) {
		return;
	}
	g_object_set_data(G_OBJECT(row), "loaded", GINT_TO_POINTER(1));
	GtkWidget* details = g_object_get_data(G_OBJECT(row), "details");
	detail(details, _("Signature valid"), yes_no(signer->signature_valid));
	detail(
	    details,
	    _("Certificate chain trusted"),
	    signer->trust_checked ? yes_no(signer->trusted) : _("Not checked")
	);
	detail(
	    details,
	    _("Detected signature level"),
	    signer->level_detected
	        ? sign_option_choice(SIGN_OPTION_LEVEL, signer->level)
	        : _("No baseline level detected")
	);
	detail(
	    details,
	    _("Signature level validated"),
	    yes_no(signer->level_validated)
	);
	char* signing_date = localized_time(signer->signing_time);
	detail(details, _("Declared signing time"), signing_date);
	g_free(signing_date);
	certificate_details(details, signer);
	if (signer->trust_error) {
		detail(
		    details,
		    _("Certificate trust error"),
		    gui_trust_error(signer->trust_error)
		);
	}
	detail(
	    details,
	    _("Revocation checked"),
	    yes_no(signer->revocation_checked)
	);
	timestamp_details(details, _("Signing timestamp"), &signer->timestamp);
	timestamp_details(details, _("Archive timestamp"), &signer->archive);
}
static void render(validation_item* item) {
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
	char* status = subtitle(item);
	gtk_label_set_text(item->status, status);
	g_free(status);
	if (!item->details_opened) {
		return;
	}
	GtkWidget* child;
	while (item->details &&
	       (child = gtk_widget_get_first_child(item->details))) {
		gtk_box_remove(GTK_BOX(item->details), child);
	}
	if (item->content) {
		char* path = g_file_get_parse_name(item->content);
		detail(item->details, _("Original content"), path);
		g_free(path);
	}
	if (!item->ok) {
		detail(item->details, _("Validation failed"), outcome(item));
	}
	bool needs_content = item->result.count &&
	                     item->result.documents[0].verification_status ==
	                         LIBSSIG_ERROR_CONTENT_REQUIRED;
	gtk_widget_set_visible(item->original, needs_content);
	if (item->error) {
		char* text = gui_error_message(item->error, GUI_ERROR_VALIDATION);
		char* reason = g_utf8_make_valid(item->error->message, -1);
		char* description = strcmp(text, reason)
		                        ? g_strjoin("\n", text, reason, NULL)
		                        : g_strdup(text);
		detail(item->details, _("Details"), description);
		g_free(description);
		g_free(reason);
		g_free(text);
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
	    item->details,
	    _("Format"),
	    (unsigned int)result->format <= LIBSSIG_FORMAT_JADES
	        ? formats[result->format]
	        : _("Unknown")
	);
	detail(
	    item->details,
	    _("Signature valid"),
	    yes_no(result->signature_valid)
	);
	detail(
	    item->details,
	    _("Certificate chain trusted"),
	    result->trust_checked ? yes_no(result->trusted) : _("Not checked")
	);
	detail(item->details, _("Detached signature"), yes_no(result->detached));
	GtkWidget* signatures = detail_list();
	gtk_box_append(GTK_BOX(item->details), signatures);
	for (size_t i = 0; i < result->count; ++i) {
		const libssig_verify_signer* signer = &result->signers[i];
		char*      title = g_strdup_printf(_("Signer %zu"), i + 1);
		GtkWidget* row = adw_expander_row_new();
		gtk_widget_set_name(row, "validation-signer");
		adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
		adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
		GtkWidget* details = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		adw_expander_row_add_row(ADW_EXPANDER_ROW(row), details);
		gtk_list_box_append(GTK_LIST_BOX(signatures), row);
		const unsigned char* cursor = signer->cert_der;
		X509*                cert = cursor
		                                ? d2i_X509(NULL, &cursor, (long)signer->cert_der_len)
		                                : NULL;
		char* name = cert ? sign_certificate_name(X509_get_subject_name(cert))
		                  : NULL;
		if (name) {
			adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), name);
		}
		X509_free(cert);
		g_free(name);
		const char* time = signer->timestamp.valid && signer->timestamp.trusted
		                       ? signer->timestamp.time
		                       : signer->signing_time;
		char*       date = localized_time(time);
		char*       summary = signer->status == LIBSSIG_OK
		                          ? g_strdup_printf(
                                  _("%s · Signed: %s"),
                                  _("Valid"),
                                  date
                              )
		                          : g_strdup(_("Validation failed"));
		adw_expander_row_set_subtitle(ADW_EXPANDER_ROW(row), summary);
		g_free(summary);
		g_free(date);
		g_object_set_data(G_OBJECT(row), "details", details);
		g_signal_connect(
		    row,
		    "notify::expanded",
		    G_CALLBACK(expand_signer),
		    (gpointer)signer
		);
		g_free(title);
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
		    SIGN_GUI_ERROR,
		    G_IO_ERROR_NOT_SUPPORTED,
		    _("Only local files can be validated")
		);
		g_free(input);
		g_free(content);
		return NULL;
	}
	if (item->template) {
		sign_flow_item document = *sign_flow_get_item(item->template, 0);
		document.options.input = input;
		document.content = content;
		sign_flow* flow = sign_flow_from_items(&document, 1, error);
		g_free(input);
		g_free(content);
		return flow;
	}
	sign_args settings = {0};
	char      detail[512];
	if (!config_load(&settings, NULL, detail, sizeof(detail))) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, detail);
		g_free(input);
		g_free(content);
		return NULL;
	}
	sign_flow_item document = {
	    .operation = SIGN_FLOW_VERIFY,
	    .options = settings.options,
	    .content = content
	};
	document.options.input = input;
	sign_flow* flow = sign_flow_from_items(&document, 1, error);
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
	if (item->discarded) {
		item_free(item);
		if (!self->closing) {
			start_next(self);
			summary_update(self);
		}
		return;
	}
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
		gtk_label_set_text(item->status, _("Waiting for another operation"));
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
	gtk_label_set_text(item->status, _("Validating…"));
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
	GtkWidget* child;
	while (item->details &&
	       (child = gtk_widget_get_first_child(item->details))) {
		gtk_box_remove(GTK_BOX(item->details), child);
	}
	sign_flow_result_clear(&item->result);
	g_clear_error(&item->error);
	item->ok = false;
	item->state = QUEUED;
	if (item->original) {
		gtk_widget_set_visible(item->original, FALSE);
	}
	g_queue_push_tail(&self->queue, item);
	gtk_label_set_text(item->status, _("Queued"));
	start_next(self);
	summary_update(self);
}
static void details_closed(AdwDialog* dialog, gpointer data) {
	SignValidation*  self = data;
	validation_item* item = g_object_get_data(
	    G_OBJECT(dialog),
	    "validation-item"
	);
	item->details_opened = false;
	--self->dialogs;
	summary_update(self);
}
static void ensure_dialog(SignValidation* self, validation_item* item) {
	if (item->dialog) {
		return;
	}
	item->details = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	item->page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_name(item->page, "validation-details");
	gtk_box_append(GTK_BOX(item->page), item->details);
	item->original = gtk_button_new_with_label(_("Select original content…"));
	gtk_widget_set_halign(item->original, GTK_ALIGN_START);
	gtk_widget_set_margin_start(item->original, 12);
	gtk_widget_set_margin_bottom(item->original, 12);
	gtk_box_append(GTK_BOX(item->page), item->original);
	GtkWidget* page_scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(
	    GTK_SCROLLED_WINDOW(page_scroll),
	    GTK_POLICY_NEVER,
	    GTK_POLICY_AUTOMATIC
	);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(page_scroll), item->page);
	item->dialog = g_object_ref_sink(adw_dialog_new());
	gtk_widget_set_name(GTK_WIDGET(item->dialog), "validation-dialog");
	adw_dialog_set_title(item->dialog, gtk_label_get_text(item->title));
	adw_dialog_set_content_width(item->dialog, 640);
	adw_dialog_set_content_height(item->dialog, 640);
	GtkWidget* toolbar = adw_toolbar_view_new();
	adw_toolbar_view_add_top_bar(
	    ADW_TOOLBAR_VIEW(toolbar),
	    adw_header_bar_new()
	);
	adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), page_scroll);
	adw_dialog_set_child(item->dialog, toolbar);
	gtk_widget_set_margin_start(item->page, 18);
	gtk_widget_set_margin_end(item->page, 18);
	gtk_widget_set_margin_bottom(item->page, 18);
	g_signal_connect_object(
	    item->dialog,
	    "closed",
	    G_CALLBACK(details_closed),
	    self,
	    0
	);
	gtk_widget_set_visible(item->original, FALSE);
	g_object_set_data(G_OBJECT(item->original), "validation-item", item);
	g_signal_connect(
	    item->original,
	    "clicked",
	    G_CALLBACK(choose_original),
	    self
	);
	g_object_set_data(G_OBJECT(item->dialog), "validation-item", item);
}
static void open_details(GtkListBox* list, GtkListBoxRow* row, gpointer data) {
	(void)list;
	SignValidation*  self = data;
	validation_item* item = g_object_get_data(G_OBJECT(row), "validation-item");
	if (!item || item->state != FINISHED || self->dialogs) {
		return;
	}
	ensure_dialog(self, item);
	item->details_opened = true;
	render(item);
	++self->dialogs;
	summary_update(self);
	sign_dialog_present(item->dialog, GTK_WIDGET(self));
}
static void discard_document(SignValidation* self, guint index) {
	validation_item* item = g_ptr_array_index(self->items, index);
	if (item->dialog && item->details_opened) {
		adw_dialog_force_close(item->dialog);
	}
	g_queue_remove(&self->queue, item);
	gtk_list_box_remove(self->list, GTK_WIDGET(item->row));
	g_ptr_array_steal_index(self->items, index);
	if (self->active == item) {
		/* The worker owns this detached snapshot until its completion callback;
		 * the replacement can already occupy the same position in the queue. */
		item->discarded = true;
	} else {
		item_free(item);
	}
}
void sign_validation_prune(SignValidation* self) {
	for (guint i = self->items->len; i > 0; --i) {
		validation_item* item = g_ptr_array_index(self->items, i - 1);
		if (file_is_missing(item->file)) {
			discard_document(self, i - 1);
		}
	}
	summary_update(self);
}
static void remove_document(GtkButton* button, gpointer data) {
	SignValidation*  self = data;
	validation_item* item = g_object_get_data(
	    G_OBJECT(button),
	    "validation-item"
	);
	if (item->state == RUNNING || self->dialogs) {
		return;
	}
	guint index;
	if (g_ptr_array_find(self->items, item, &index)) {
		discard_document(self, index);
	}

	summary_update(self);
}
static void add_files(
    SignValidation* self,
    GListModel*     files,
    GPtrArray*      templates
) {
	if (self->closing) {
		return;
	}
	sign_validation_prune(self);
	gtk_widget_set_visible(GTK_WIDGET(self->notice), FALSE);
	for (guint i = 0; i < g_list_model_get_n_items(files); ++i) {
		GFile* file = g_list_model_get_item(files, i);
		if (!G_IS_FILE(file)) {
			if (file) {
				g_object_unref(file);
			}
			continue;
		}
		if (file_is_missing(file)) {
			g_object_unref(file);
			continue;
		}
		guint position = self->items->len;
		for (guint j = self->items->len; j > 0; --j) {
			validation_item* previous = g_ptr_array_index(self->items, j - 1);
			if (g_file_equal(previous->file, file)) {
				position = j - 1;
				discard_document(self, j - 1);
			}
		}
		validation_item* item = g_new0(validation_item, 1);
		item->file = file;
		if (templates) {
			item->template = g_ptr_array_index(templates, i);
			g_ptr_array_index(templates, i) = NULL;
			const sign_flow_item* document = sign_flow_get_item(
			    item->template,
			    0
			);
			if (document->content) {
				item->content = g_file_new_for_path(document->content);
			}
		}
		item->row = GTK_LIST_BOX_ROW(gtk_list_box_row_new());
		gtk_widget_add_css_class(GTK_WIDGET(item->row), "document-row");
		gtk_list_box_row_set_activatable(item->row, TRUE);
		g_object_set_data(G_OBJECT(item->row), "validation-item", item);
		GtkWidget* row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
		GtkWidget* labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
		gtk_widget_set_hexpand(labels, TRUE);
		gtk_widget_set_valign(labels, GTK_ALIGN_CENTER);
		char* basename = g_file_get_basename(file);
		char* display = basename ? g_filename_display_name(basename)
		                         : g_file_get_parse_name(file);
		item->title = GTK_LABEL(gtk_label_new(display));
		gtk_widget_set_name(GTK_WIDGET(item->title), "validation-filename");
		gtk_label_set_xalign(item->title, 0);
		gtk_label_set_ellipsize(item->title, PANGO_ELLIPSIZE_MIDDLE);
		g_free(display);
		g_free(basename);
		gtk_box_append(GTK_BOX(labels), GTK_WIDGET(item->title));
		GtkWidget* status_line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
		item->spinner = gtk_spinner_new();
		item->icon = gtk_image_new();
		gtk_image_set_pixel_size(GTK_IMAGE(item->icon), 14);
		gtk_box_append(GTK_BOX(status_line), item->spinner);
		gtk_box_append(GTK_BOX(status_line), item->icon);
		item->status = GTK_LABEL(gtk_label_new(_("Queued")));
		gtk_widget_set_name(GTK_WIDGET(item->status), "validation-status");
		gtk_label_set_xalign(item->status, 0);
		gtk_label_set_wrap(item->status, TRUE);
		gtk_widget_add_css_class(GTK_WIDGET(item->status), "dim-label");
		gtk_widget_add_css_class(GTK_WIDGET(item->status), "caption");
		gtk_box_append(GTK_BOX(status_line), GTK_WIDGET(item->status));
		gtk_box_append(GTK_BOX(labels), status_line);
		gtk_box_append(GTK_BOX(row_box), labels);
		gtk_list_box_row_set_child(item->row, row_box);
		sign_file_actions_attach(
		    GTK_WIDGET(item->row),
		    item->file,
		    false,
		    false,
		    NULL,
		    NULL
		);
		gtk_box_append(GTK_BOX(row_box), sign_file_location_button());
		item->remove = gtk_button_new_from_icon_name("window-close-symbolic");
		gtk_widget_set_name(item->remove, "validation-remove-document");
		gtk_widget_add_css_class(item->remove, "flat");
		gtk_widget_set_valign(item->remove, GTK_ALIGN_CENTER);
		gtk_widget_set_tooltip_text(item->remove, _("Remove document"));
		g_object_set_data(G_OBJECT(item->remove), "validation-item", item);
		g_signal_connect(
		    item->remove,
		    "clicked",
		    G_CALLBACK(remove_document),
		    self
		);
		gtk_box_append(GTK_BOX(row_box), item->remove);
		gtk_list_box_insert(self->list, GTK_WIDGET(item->row), position);
		g_ptr_array_insert(self->items, position, item);
		g_queue_push_tail(&self->queue, item);
	}
	start_next(self);
	summary_update(self);
}
void sign_validation_add_files(SignValidation* self, GListModel* files) {
	add_files(self, files, NULL);
}
bool sign_validation_add_flow(
    SignValidation*  self,
    const sign_flow* flow,
    GError**         error
) {
	if (self->closing) {
		return false;
	}
	size_t     count = flow ? sign_flow_count(flow) : 0;
	GPtrArray* templates = g_ptr_array_new_with_free_func(
	    (GDestroyNotify)sign_flow_free
	);
	GListStore* files = g_list_store_new(G_TYPE_FILE);
	bool        ok = count > 0;
	for (size_t n = 0; ok && n < count; ++n) {
		const sign_flow_item* item = sign_flow_get_item(flow, n);
		if (item->operation != SIGN_FLOW_VERIFY) {
			ok = false;
			break;
		}
		sign_flow* document = sign_flow_from_items(item, 1, error);
		if (!document) {
			ok = false;
			break;
		}
		g_ptr_array_add(templates, document);
		GFile* file = g_file_new_for_path(item->options.input);
		g_list_store_append(files, file);
		g_object_unref(file);
	}
	if (ok) {
		add_files(self, G_LIST_MODEL(files), templates);
	} else if (!error || !*error) {
		g_set_error_literal(
		    error,
		    SIGN_GUI_ERROR,
		    G_IO_ERROR_INVALID_ARGUMENT,
		    _("Only verification flows can be added to validation")
		);
	}
	g_object_unref(files);
	g_ptr_array_unref(templates);
	return ok;
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
	char* text = gui_error_message(error, GUI_ERROR_FILES);
	gtk_label_set_text(self->notice, text);
	g_free(text);
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
static void dropped_files(GtkWidget* widget, GListModel* files, gpointer data) {
	(void)data;
	sign_validation_add_files(SIGN_VALIDATION(widget), files);
}
static GCancellable* drop_cancel(GtkWidget* widget, gpointer data) {
	(void)data;
	return SIGN_VALIDATION(widget)->cancellable;
}
static void drop_state(
    GtkWidget*    widget,
    bool          start,
    const GError* error,
    gpointer      data
) {
	(void)data;
	SignValidation* self = SIGN_VALIDATION(widget);
	if (start) {
		++self->dialogs;
	} else {
		--self->dialogs;
	}
	dialog_error(self, error);
	if (!self->closing) {
		summary_update(self);
	}
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
	GtkWidget* sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
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
	gtk_box_append(GTK_BOX(sidebar), self->drop_area);
	self->notice = GTK_LABEL(gtk_label_new(NULL));
	gtk_label_set_wrap(self->notice, TRUE);
	gtk_label_set_wrap_mode(self->notice, PANGO_WRAP_WORD_CHAR);
	gtk_widget_add_css_class(GTK_WIDGET(self->notice), "error");
	gtk_widget_set_visible(GTK_WIDGET(self->notice), FALSE);
	gtk_box_append(GTK_BOX(sidebar), GTK_WIDGET(self->notice));
	g_signal_connect(
	    self->drop_area,
	    "clicked",
	    G_CALLBACK(choose_files),
	    self
	);
	self->controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	self->summary = GTK_LABEL(gtk_label_new(NULL));
	gtk_widget_add_css_class(GTK_WIDGET(self->summary), "dim-label");
	gtk_label_set_wrap(self->summary, TRUE);
	gtk_label_set_xalign(self->summary, 0);
	gtk_widget_set_hexpand(GTK_WIDGET(self->summary), TRUE);
	gtk_box_append(GTK_BOX(self->controls), GTK_WIDGET(self->summary));
	self->clear = gtk_button_new_with_label(_("Clear results"));
	gtk_widget_set_name(self->clear, "validation-clear");
	gtk_widget_set_halign(self->clear, GTK_ALIGN_END);
	gtk_box_append(GTK_BOX(self->controls), self->clear);
	g_signal_connect(self->clear, "clicked", G_CALLBACK(clear_results), self);
	gtk_box_append(GTK_BOX(sidebar), self->controls);
	self->scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(
	    GTK_SCROLLED_WINDOW(self->scroll),
	    GTK_POLICY_NEVER,
	    GTK_POLICY_AUTOMATIC
	);
	gtk_widget_set_vexpand(self->scroll, TRUE);
	self->list = GTK_LIST_BOX(gtk_list_box_new());
	gtk_widget_set_valign(GTK_WIDGET(self->list), GTK_ALIGN_START);
	gtk_widget_set_name(GTK_WIDGET(self->list), "validation-results");
	gtk_list_box_set_selection_mode(self->list, GTK_SELECTION_NONE);
	g_signal_connect(
	    self->list,
	    "row-activated",
	    G_CALLBACK(open_details),
	    self
	);
	gtk_widget_add_css_class(GTK_WIDGET(self->list), "boxed-list");
	gtk_scrolled_window_set_child(
	    GTK_SCROLLED_WINDOW(self->scroll),
	    GTK_WIDGET(self->list)
	);
	gtk_box_append(GTK_BOX(sidebar), self->scroll);
	gtk_widget_set_vexpand(sidebar, TRUE);
	gtk_box_append(GTK_BOX(self), sidebar);

	sign_file_drop_attach(
	    GTK_WIDGET(self),
	    dropped_files,
	    drop_cancel,
	    drop_state,
	    NULL
	);
	summary_update(self);
}
GtkWidget* sign_validation_new(void) {
	return g_object_new(SIGN_TYPE_VALIDATION, NULL);
}
