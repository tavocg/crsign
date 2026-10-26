/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "helpers.h"
#include <glib/gstdio.h>
#include <sign/config.h>
#include <sign/gui/i18n.h>
#include <sign/gui/validation.h>

static GFile* fixture(const char* root, const char* name) {
	char*  path = g_build_filename(root, name, NULL);
	GFile* file = g_file_new_for_path(path);
	g_free(path);
	return file;
}
static void wait_done(SignValidation* self) {
	gint64 deadline = g_get_monotonic_time() + 15 * G_TIME_SPAN_SECOND;
	do {
		while (g_main_context_iteration(NULL, FALSE)) {
		}
		g_usleep(1000);
	} while (sign_validation_is_busy(self) &&
	         g_get_monotonic_time() < deadline);
	g_assert_false(sign_validation_is_busy(self));
}
static gboolean heartbeat(gpointer data) {
	++*(guint*)data;
	return G_SOURCE_CONTINUE;
}
static bool contains(GtkWidget* widget, const char* text) {
	if (GTK_IS_LABEL(widget) &&
	    strstr(gtk_label_get_text(GTK_LABEL(widget)), text)) {
		return true;
	}
	for (GtkWidget* child = gtk_widget_get_first_child(widget); child;
	     child = gtk_widget_get_next_sibling(child)) {
		if (contains(child, text)) {
			return true;
		}
	}
	return false;
}
int main(int argc, char** argv) {
	g_assert_cmpint(argc, ==, 3);
	const char* root = argv[1];
	g_assert_cmpint(g_mkdir_with_parents(argv[2], 0700), ==, 0);
	gui_i18n_init();
	adw_init();
	adw_style_manager_set_color_scheme(
	    adw_style_manager_get_default(),
	    ADW_COLOR_SCHEME_FORCE_DARK
	);
	AdwApplication* app = adw_application_new(
	    "org.crsign.Sign.ValidationTest",
	    G_APPLICATION_NON_UNIQUE
	);
	g_assert_true(g_application_register(G_APPLICATION(app), NULL, NULL));
	GtkWidget* window = adw_application_window_new(GTK_APPLICATION(app));
	gtk_window_set_default_size(GTK_WINDOW(window), 760, 650);
	GtkWidget* view = sign_validation_new();
	GtkWidget* layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_append(
	    GTK_BOX(layout),
	    sign_validation_header_clear(SIGN_VALIDATION(view))
	);
	gtk_box_append(GTK_BOX(layout), view);
	adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), layout);
	gtk_window_present(GTK_WINDOW(window));
	settle();
	SignValidation* self = SIGN_VALIDATION(view);
	g_assert_cmpuint(sign_validation_count(self), ==, 0);
	g_assert_false(sign_validation_is_busy(self));
	g_assert_nonnull(named(view, "validation-choose"));
	snapshot(window, argv[2], "validation-empty.png");
	GListModel*    controllers = gtk_widget_observe_controllers(view);
	GtkDropTarget* target = NULL;
	for (guint i = 0; i < g_list_model_get_n_items(controllers); ++i) {
		GtkEventController* controller = g_list_model_get_item(controllers, i);
		if (GTK_IS_DROP_TARGET(controller)) {
			target = g_object_ref(GTK_DROP_TARGET(controller));
		}
		g_object_unref(controller);
	}
	g_object_unref(controllers);
	g_assert_nonnull(target);
	GFile* files[] = {
	    fixture(root, "payload.bin"),
	    fixture(root, "signed ñ & record.p7m"),
	    fixture(root, "missing.p7m"),
	    fixture(root, "detached.p7s")
	};
	GValue value = G_VALUE_INIT;
	g_value_init(&value, GDK_TYPE_FILE_LIST);
	g_value_take_boxed(
	    &value,
	    gdk_file_list_new_from_array(files, G_N_ELEMENTS(files))
	);
	gboolean accepted = FALSE;
	guint    ticks = 0;
	guint    timer = g_timeout_add(1, heartbeat, &ticks);
	g_signal_emit_by_name(target, "drop", &value, 0.0, 0.0, &accepted);
	g_assert_true(accepted);
	g_assert_true(sign_validation_is_busy(self));
	g_assert_false(
	    gtk_widget_get_sensitive(sign_validation_header_clear(self))
	);
	GListStore* extra = g_list_store_new(G_TYPE_FILE);
	GFile*      untrusted = fixture(root, "untrusted.p7m");
	g_list_store_append(extra, untrusted);
	g_list_store_append(extra, files[1]);
	sign_validation_add_files(self, G_LIST_MODEL(extra));
	wait_done(self);
	g_source_remove(timer);
	g_assert_cmpuint(ticks, >, 0);
	g_assert_cmpuint(sign_validation_count(self), ==, 4);
	g_assert_cmpstr(
	    gtk_label_get_text(GTK_LABEL(named(view, "validation-summary"))),
	    ==,
	    "Válidos: 1/4"
	);
	g_assert_false(sign_validation_result(self, 0)->ok);
	const sign_flow_document_result* valid = sign_validation_result(self, 1);
	g_assert_true(valid->ok);
	g_assert_true(valid->verification.signature_valid);
	g_assert_true(valid->verification.trusted);
	g_assert_cmpuint(valid->verification.count, ==, 2);
	for (size_t i = 0; i < 2; ++i) {
		g_assert_true(valid->verification.signers[i].signature_valid);
		g_assert_true(valid->verification.signers[i].trusted);
	}
	g_assert_false(sign_validation_result(self, 2)->ok);
	g_assert_cmpint(
	    sign_validation_result(self, 2)->verification_status,
	    ==,
	    LIBSSIG_ERROR_CONTENT_REQUIRED
	);
	const sign_flow_document_result* unknown = sign_validation_result(self, 3);
	g_assert_false(unknown->ok);
	g_assert_true(unknown->verification.signature_valid);
	g_assert_false(unknown->verification.trusted);
	g_assert_true(sign_validation_result(self, 1)->ok);
	g_assert_false(contains(view, "CN=Flow signer"));
	GtkListBox*    list = GTK_LIST_BOX(named(view, "validation-results"));
	GtkListBoxRow* row = gtk_list_box_get_row_at_index(list, 1);
	g_assert_cmpstr(
	    gtk_label_get_text(
	        GTK_LABEL(named(GTK_WIDGET(row), "validation-filename"))
	    ),
	    ==,
	    "signed ñ & record.p7m"
	);
	const char* signed_at = valid->verification.signers[0].signing_time;
	g_assert_nonnull(signed_at);
	const char* other_time = valid->verification.signers[1].signing_time;
	g_assert_nonnull(other_time);
	if (strcmp(other_time, signed_at) > 0) {
		signed_at = other_time;
	}
	g_assert_cmpuint(strlen(signed_at), ==, 20);
	GDateTime* utc = g_date_time_new_from_iso8601(signed_at, NULL);
	g_assert_nonnull(utc);
	GDateTime* local = g_date_time_to_local(utc);
	char*      date = g_date_time_format(local, "%x %X");
	char*      expected = g_strdup_printf("Válido · Firmado: %s", date);
	g_assert_cmpstr(
	    gtk_label_get_text(
	        GTK_LABEL(named(GTK_WIDGET(row), "validation-status"))
	    ),
	    ==,
	    expected
	);
	g_free(expected);
	g_free(date);
	g_date_time_unref(local);
	g_date_time_unref(utc);
	GtkListBoxRow* failed = gtk_list_box_get_row_at_index(list, 0);
	g_assert_cmpstr(
	    gtk_label_get_text(
	        GTK_LABEL(named(GTK_WIDGET(failed), "validation-status"))
	    ),
	    ==,
	    "Archivo sin firma o formato no admitido"
	);
	g_assert_null(named(view, "validation-signer"));
	snapshot(window, argv[2], "validation-queue.png");
	g_assert_null(strstr(
	    gtk_label_get_text(GTK_LABEL(named(
	        GTK_WIDGET(gtk_list_box_get_row_at_index(list, 3)),
	        "validation-status"
	    ))),
	    "Firmado:"
	));
	g_signal_emit_by_name(list, "row-activated", row);
	settle();
	AdwDialog* dialog = adw_application_window_get_visible_dialog(
	    ADW_APPLICATION_WINDOW(window)
	);
	g_assert_nonnull(dialog);
	g_assert_cmpstr(adw_dialog_get_title(dialog), ==, "signed ñ & record.p7m");
	GtkWidget* page = GTK_WIDGET(dialog);
	g_assert_false(contains(page, "Archivo"));
	AdwExpanderRow* signer = ADW_EXPANDER_ROW(named(page, "validation-signer"));
	g_assert_nonnull(signer);
	g_assert_false(adw_expander_row_get_expanded(signer));
	g_assert_false(contains(page, "Huella SHA-256"));
	GBytes* collapsed_arrow = arrow_shape(window, GTK_WIDGET(signer));
	adw_expander_row_set_expanded(signer, TRUE);
	GBytes* expanded_arrow = arrow_shape(window, GTK_WIDGET(signer));
	g_assert_false(g_bytes_equal(collapsed_arrow, expanded_arrow));
	adw_expander_row_set_expanded(signer, FALSE);
	GBytes* closed_again = arrow_shape(window, GTK_WIDGET(signer));
	g_assert_true(g_bytes_equal(collapsed_arrow, closed_again));
	g_bytes_unref(collapsed_arrow);
	g_bytes_unref(expanded_arrow);
	g_bytes_unref(closed_again);
	snapshot(window, argv[2], "validation-results.png");
	gtk_window_set_default_size(GTK_WINDOW(window), 400, 700);
	snapshot(window, argv[2], "validation-narrow.png");
	no_nested_expanders(page, false);
	adw_dialog_force_close(dialog);
	settle();
	g_signal_emit_by_name(list, "row-activated", failed);
	settle();
	dialog = adw_application_window_get_visible_dialog(
	    ADW_APPLICATION_WINDOW(window)
	);
	g_assert_nonnull(dialog);
	g_assert_true(
	    contains(GTK_WIDGET(dialog), "Archivo sin firma o formato no admitido")
	);
	adw_dialog_force_close(dialog);
	settle();
	gtk_window_set_default_size(GTK_WINDOW(window), 1000, 750);
	settle();
	g_signal_emit_by_name(list, "row-activated", failed);
	settle();
	dismiss_dialog_background(window);

	sign_validation_set_content(self, 2, files[0]);
	wait_done(self);
	g_assert_true(sign_validation_result(self, 2)->ok);
	g_assert_true(sign_validation_result(self, 2)->verification.detached);
	GFile* wrong = fixture(root, "wrong.bin");
	sign_validation_set_content(self, 2, wrong);
	wait_done(self);
	g_assert_false(sign_validation_result(self, 2)->ok);
	sign_validation_set_content(self, 2, files[0]);
	wait_done(self);
	g_assert_true(sign_validation_result(self, 2)->ok);
	GtkWidget* remove_button = named(
	    GTK_WIDGET(gtk_list_box_get_row_at_index(list, 2)),
	    "validation-remove-document"
	);
	g_assert_nonnull(remove_button);
	g_signal_emit_by_name(remove_button, "clicked");
	g_assert_cmpuint(sign_validation_count(self), ==, 3);
	g_assert_true(gtk_widget_get_sensitive(sign_validation_header_clear(self)));
	g_signal_emit_by_name(sign_validation_header_clear(self), "clicked");
	g_assert_cmpuint(sign_validation_count(self), ==, 0);
	g_assert_null(adw_application_window_get_visible_dialog(
	    ADW_APPLICATION_WINDOW(window)
	));
	g_assert_null(gtk_widget_get_first_child(GTK_WIDGET(list)));
	/* A temporary unroot/reparent must not permanently disable file intake or
	 * abandon queued work, even when a worker finishes while detached. */
	sign_validation_add_files(self, G_LIST_MODEL(extra));
	g_object_ref(view);
	adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), NULL);
	settle();
	adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), view);
	g_object_unref(view);
	wait_done(self);
	g_assert_cmpuint(sign_validation_count(self), ==, 2);
	g_assert_true(sign_validation_result(self, 1)->ok);
	g_signal_emit_by_name(sign_validation_header_clear(self), "clicked");
	sign_validation_add_files(self, G_LIST_MODEL(extra));
	wait_done(self);
	g_assert_cmpuint(sign_validation_count(self), ==, 2);
	g_assert_true(sign_validation_result(self, 1)->ok);
	g_signal_emit_by_name(sign_validation_header_clear(self), "clicked");
	/* Flow intake owns its options and supplies detached content immediately.
	 */
	char*          detached = g_file_get_path(files[3]);
	char*          original = g_file_get_path(files[0]);
	char*          anchor = g_build_filename(root, "anchor.der", NULL);
	sign_flow_item document = {
	    .operation = SIGN_FLOW_VERIFY,
	    .content = original
	};
	document.options.input = detached;
	document.options.ca_file = anchor;
	GError*    error = NULL;
	sign_flow* flow = sign_flow_from_items(&document, 1, &error);
	g_assert_no_error(error);
	g_assert_true(sign_validation_add_flow(self, flow, &error));
	g_assert_no_error(error);
	sign_flow_free(flow);
	g_free(detached);
	g_free(original);
	g_free(anchor);
	wait_done(self);
	g_assert_cmpuint(sign_validation_count(self), ==, 1);
	g_assert_true(sign_validation_result(self, 0)->ok);
	g_assert_true(sign_validation_result(self, 0)->verification.detached);
	g_assert_true(sign_validation_result(self, 0)->verification.trusted);
	g_signal_emit_by_name(sign_validation_header_clear(self), "clicked");
	/* Re-adding a changed path replaces its previous validation result. */
	char* changed_path = g_build_filename(root, "changed.p7m", NULL);
	char* original_path = g_file_get_path(files[1]);
	char* bytes = NULL;
	gsize length = 0;
	g_assert_true(g_file_get_contents(original_path, &bytes, &length, NULL));
	g_assert_true(g_file_set_contents(changed_path, bytes, length, NULL));
	GFile*      changed = g_file_new_for_path(changed_path);
	GListStore* replacements = g_list_store_new(G_TYPE_FILE);
	g_list_store_append(replacements, changed);
	sign_validation_add_files(self, G_LIST_MODEL(replacements));
	/* Replacing the active item must safely discard the older worker result. */
	sign_validation_add_files(self, G_LIST_MODEL(replacements));
	wait_done(self);
	g_assert_cmpuint(sign_validation_count(self), ==, 1);
	g_assert_true(sign_validation_result(self, 0)->ok);
	g_assert_true(
	    g_file_set_contents(changed_path, "broken signature", -1, NULL)
	);
	sign_validation_add_files(self, G_LIST_MODEL(replacements));
	wait_done(self);
	g_assert_cmpuint(sign_validation_count(self), ==, 1);
	g_assert_false(sign_validation_result(self, 0)->ok);
	g_assert_cmpint(g_remove(changed_path), ==, 0);
	sign_validation_add_files(self, G_LIST_MODEL(replacements));
	wait_done(self);
	g_assert_cmpuint(sign_validation_count(self), ==, 0);
	g_object_unref(replacements);
	g_object_unref(changed);
	g_free(changed_path);
	g_free(original_path);
	g_free(bytes);
	/* Closing with work pending cancels the queue and releases the page once
	 * its current worker returns; the callback must not access destroyed UI. */
	sign_validation_add_files(self, G_LIST_MODEL(extra));
	sign_validation_add_files(self, G_LIST_MODEL(extra));
	gpointer weak = view;
	g_object_add_weak_pointer(G_OBJECT(view), &weak);
	gtk_window_destroy(GTK_WINDOW(window));
	gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
	while (weak && g_get_monotonic_time() < deadline) {
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	}
	g_assert_null(weak);
	g_object_unref(target);
	g_value_unset(&value);
	for (guint i = 0; i < G_N_ELEMENTS(files); ++i) {
		g_object_unref(files[i]);
	}
	g_object_unref(untrusted);
	g_object_unref(wrong);
	g_object_unref(extra);
	g_object_unref(app);
	return 0;
}
