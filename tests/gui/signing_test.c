/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "helpers.h"
#include <glib/gstdio.h>
#include <math.h>
#include <sign/gui/i18n.h>
#include <sign/gui/pdf_view.h>
#include <sign/gui/signing.h>

static guint    pins;
static bool     cancel_pin, close_parent;
static gboolean answer_pin(gpointer data) {
	GtkApplication* app = data;
	for (GList* n = gtk_application_get_windows(app); n; n = n->next) {
		GtkWidget* entry = named(n->data, "sign-pin-entry");
		if (entry) {
			gtk_editable_set_text(GTK_EDITABLE(entry), "1234");
			++pins;
			if (close_parent) {
				gtk_window_destroy(
				    gtk_window_get_transient_for(GTK_WINDOW(n->data))
				);
				break;
			}
			g_signal_emit_by_name(
			    named(
			        n->data,
			        cancel_pin ? "sign-pin-cancel" : "sign-pin-confirm"
			    ),
			    "clicked"
			);
			break;
		}
	}
	return G_SOURCE_CONTINUE;
}
static void wait_done(SignSigning* self) {
	gint64 deadline = g_get_monotonic_time() + 20 * G_TIME_SPAN_SECOND;
	do {
		while (g_main_context_iteration(NULL, FALSE)) {
		}
		g_usleep(1000);
	} while ((sign_signing_is_loading(self) || sign_signing_is_busy(self)) &&
	         g_get_monotonic_time() < deadline);
	g_assert_false(sign_signing_is_loading(self));
	g_assert_false(sign_signing_is_busy(self));
}
static void add(SignSigning* self, const char* path) {
	GListStore* files = g_list_store_new(G_TYPE_FILE);
	GFile*      file = g_file_new_for_path(path);
	g_list_store_append(files, file);
	sign_signing_add_files(self, G_LIST_MODEL(files));
	g_object_unref(files);
	g_object_unref(file);
	wait_done(self);
}
/* Drive real mouse events on the isolated Xvfb display, when available. */
static void mouse_place(GtkWidget* window, SignSigning* self, bool drag) {
	char* tool = g_find_program_in_path("xdotool");
	if (!tool) {
		return;
	}
	char* id = NULL;
	char* search[] = {tool, "search", "--name", "^CRSign Signing Test$", NULL};
	int   status = 0;
	g_assert_true(g_spawn_sync(
	    NULL,
	    search,
	    NULL,
	    G_SPAWN_DEFAULT,
	    NULL,
	    NULL,
	    &id,
	    NULL,
	    &status,
	    NULL
	));
	g_assert_cmpint(status, ==, 0);
	g_strstrip(id);
	GtkWidget*      canvas = named(GTK_WIDGET(self), "sign-pdf-canvas");
	graphene_rect_t bounds;
	g_assert_true(gtk_widget_compute_bounds(canvas, window, &bounds));
	int   x = (int)bounds.origin.x + 80, y = (int)bounds.origin.y + 90;
	char* command = drag ? g_strdup_printf(
	                           "%s mousemove --window %s %d %d mousedown 1 "
	                           "mousemove --window %s %d %d mouseup 1",
	                           tool,
	                           id,
	                           x,
	                           y,
	                           id,
	                           x + 180,
	                           y + 90
	                       )
	                     : g_strdup_printf(
	                           "%s mousemove --window %s %d %d click 1",
	                           tool,
	                           id,
	                           x,
	                           y
	                       );
	g_assert_true(
	    g_spawn_command_line_sync(command, NULL, NULL, &status, NULL)
	);
	g_assert_cmpint(status, ==, 0);
	settle();
	g_assert_cmpuint(sign_signing_appearance(self, 0)->page_number, ==, 1);
	if (drag) {
		g_assert_cmpfloat(sign_signing_appearance(self, 0)->width, >, 144);
	}
	g_free(command);
	g_free(id);
	g_free(tool);
}
int main(int argc, char** argv) {
	g_assert_cmpint(argc, ==, 3);
	gui_i18n_init();
	adw_init();
	g_mkdir_with_parents(argv[2], 0700);
	AdwApplication* app = adw_application_new(
	    "org.crsign.Sign.SigningTest",
	    G_APPLICATION_NON_UNIQUE
	);
	g_assert_true(g_application_register(G_APPLICATION(app), NULL, NULL));
	GtkWidget* window = adw_application_window_new(GTK_APPLICATION(app));
	gtk_window_set_default_size(GTK_WINDOW(window), 820, 760);
	gtk_window_set_title(GTK_WINDOW(window), "CRSign Signing Test");
	GtkWidget*   box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	SignSigning* self = SIGN_SIGNING(sign_signing_new());
	gtk_box_append(GTK_BOX(box), sign_signing_profile_selector(self));
	gtk_box_append(GTK_BOX(box), GTK_WIDGET(self));
	adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), box);
	gtk_window_present(GTK_WINDOW(window));
	settle();
	g_assert_cmpuint(sign_signing_count(self), ==, 0);
	snapshot(window, argv[2], "sign-empty.png");
	char*              pdf = g_build_filename(argv[1], "page.pdf", NULL);
	char*              payload = g_build_filename(argv[1], "payload.bin", NULL);
	char*              image = g_build_filename(argv[1], "image.svg", NULL);
	GFile*             file = g_file_new_for_path(pdf);
	GError*            error = NULL;
	GCancellable*      cancel = g_cancellable_new();
	sign_pdf_document* document = sign_pdf_open(file, cancel, &error);
	g_assert_no_error(error);
	g_assert_cmpuint(sign_pdf_pages(document), ==, 2);
	double w, h;
	g_assert_true(sign_pdf_size(document, 2, &w, &h));
	g_assert_cmpfloat(w, ==, 800);
	g_assert_cmpfloat(h, ==, 480);
	sign_pdf_unref(document);
	g_object_unref(cancel);
	g_object_unref(file);
	GListModel* controllers = gtk_widget_observe_controllers(GTK_WIDGET(self));
	GtkDropTarget* target = NULL;
	for (guint i = 0; i < g_list_model_get_n_items(controllers); ++i) {
		GObject* controller = g_list_model_get_item(controllers, i);
		if (GTK_IS_DROP_TARGET(controller)) {
			target = g_object_ref(GTK_DROP_TARGET(controller));
		}
		g_object_unref(controller);
	}
	g_object_unref(controllers);
	g_assert_nonnull(target);
	GFile* dropped = g_file_new_for_path(pdf);
	GValue value = G_VALUE_INIT;
	g_value_init(&value, GDK_TYPE_FILE_LIST);
	g_value_take_boxed(&value, gdk_file_list_new_from_array(&dropped, 1));
	gboolean accepted = FALSE;
	g_signal_emit_by_name(target, "drop", &value, 0.0, 0.0, &accepted);
	g_assert_true(accepted);
	g_value_unset(&value);
	g_object_unref(target);
	g_object_unref(dropped);
	wait_done(self);
	g_assert_true(sign_signing_is_reader(self));
	settle();
	mouse_place(window, self, false);
	sign_signing_remove_appearance(self);
	mouse_place(window, self, true);
	sign_signing_remove_appearance(self);
	sign_signing_set_image(self, image);
	wait_done(self);
	g_assert_true(sign_signing_place(self, 30, 40, 0, 0, &error));
	g_assert_no_error(error);
	const libssig_pdf_appearance* a = sign_signing_appearance(self, 0);
	g_assert_cmpfloat(a->width, ==, 144);
	g_assert_cmpfloat(a->height, ==, 72);
	g_assert_true(sign_signing_place(self, 30, 40, 1, 1, &error));
	g_assert_cmpfloat(a->width, ==, 24);
	g_assert_cmpfloat(a->height, ==, 12);
	g_assert_false(sign_signing_place(self, 611, 791, 0, 0, &error));
	g_clear_error(&error);
	sign_signing_set_page(self, 2);
	wait_done(self);
	g_assert_true(sign_signing_place(self, 40, 50, 300, 150, &error));
	g_assert_cmpuint(a->page_number, ==, 2);
	gtk_window_set_default_size(GTK_WINDOW(window), 420, 760);
	settle();
	g_assert_cmpint(gtk_widget_get_width(window), <=, 420);
	snapshot(window, argv[2], "sign-reader-narrow.png");
	gtk_window_set_default_size(GTK_WINDOW(window), 820, 760);
	snapshot(window, argv[2], "sign-reader.png");
	sign_signing_remove_appearance(self);
	g_assert_cmpuint(a->page_number, ==, 0);
	/* Both raster decoders feed the same physical geometry as SVG. */
	GdkPixbuf* pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 200, 100);
	gdk_pixbuf_fill(pixbuf, 0x338855ff);
	const char* formats[] = {"png", "jpeg"};
	for (size_t n = 0; n < G_N_ELEMENTS(formats); ++n) {
		char* path = g_strdup_printf("%s/image.%s", argv[1], formats[n]);
		g_assert_true(gdk_pixbuf_save(pixbuf, path, formats[n], &error, NULL));
		g_assert_no_error(error);
		sign_signing_set_image(self, path);
		wait_done(self);
		g_assert_true(sign_signing_place(self, 40, 50, 0, 0, &error));
		g_assert_cmpfloat(a->width / a->height, ==, 2);
		g_free(path);
	}
	g_object_unref(pixbuf);
	sign_signing_set_image(self, image);
	wait_done(self);
	char* bad = g_build_filename(argv[1], "invalid.svg", NULL);
	sign_signing_set_image(self, bad);
	wait_done(self);
	g_assert_cmpuint(a->page_number, ==, 2);
	g_free(bad);
	sign_signing_show_queue(self);
	add(self, payload);
	g_assert_false(sign_signing_is_reader(self));
	g_assert_cmpuint(sign_signing_count(self), ==, 2);
	g_assert_true(
	    gtk_widget_get_visible(named(GTK_WIDGET(self), "sign-warning"))
	);
	GtkDropDown* profiles = GTK_DROP_DOWN(sign_signing_profile_selector(self));
	gtk_drop_down_set_selected(profiles, 1);
	sign_flow* flow = sign_signing_build_flow(self, &error);
	g_assert_no_error(error);
	g_assert_nonnull(flow);
	g_assert_cmpuint(sign_flow_count(flow), ==, 2);
	g_assert_cmpuint(
	    sign_flow_document(flow, 0)->appearance.page_number,
	    ==,
	    2
	);
	g_assert_cmpuint(
	    sign_flow_document(flow, 1)->appearance.page_number,
	    ==,
	    0
	);
	char* json = sign_flow_to_json(flow, &error);
	g_assert_no_error(error);
	g_assert_null(strstr(json, "1234"));
	free(json);
	sign_flow_free(flow);
	snapshot(window, argv[2], "sign-queue.png");
	guint timer = g_timeout_add(10, answer_pin, app);
	sign_signing_run(self);
	wait_done(self);
	g_source_remove(timer);
	g_assert_cmpuint(pins, ==, 1);
	char* output = g_build_filename(argv[1], "page-signed.pdf", NULL);
	g_assert_true(g_file_test(output, G_FILE_TEST_IS_REGULAR));
	g_free(output);
	output = g_build_filename(argv[1], "payload-signed.bin", NULL);
	g_assert_true(g_file_test(output, G_FILE_TEST_IS_REGULAR));
	g_free(output);
	/* Existing output must stop before asking for a second PIN. */
	sign_signing_run(self);
	wait_done(self);
	g_assert_cmpuint(pins, ==, 1);
	/* Cancellation of a runtime PIN leaves the document untouched. */
	GtkWidget* second = adw_application_window_new(GTK_APPLICATION(app));
	gtk_window_set_default_size(GTK_WINDOW(second), 760, 650);
	SignSigning* cancelled = SIGN_SIGNING(sign_signing_new());
	adw_application_window_set_content(
	    ADW_APPLICATION_WINDOW(second),
	    GTK_WIDGET(cancelled)
	);
	gtk_window_present(GTK_WINDOW(second));
	char* cancel_file = g_build_filename(argv[1], "cancel.bin", NULL);
	g_assert_true(g_file_set_contents(cancel_file, "cancel", -1, NULL));
	add(cancelled, cancel_file);
	cancel_pin = true;
	timer = g_timeout_add(10, answer_pin, app);
	sign_signing_run(cancelled);
	wait_done(cancelled);
	g_source_remove(timer);
	g_assert_cmpuint(pins, ==, 2);
	output = g_build_filename(argv[1], "cancel-signed.bin", NULL);
	g_assert_false(g_file_test(output, G_FILE_TEST_EXISTS));
	g_free(output);
	g_free(cancel_file);
	/* Closing the parent during a PIN request must release the worker too. */
	g_object_ref(cancelled);
	close_parent = true;
	timer = g_timeout_add(10, answer_pin, app);
	sign_signing_run(cancelled);
	wait_done(cancelled);
	g_source_remove(timer);
	g_assert_cmpuint(pins, ==, 3);
	g_object_unref(cancelled);
	gtk_window_destroy(GTK_WINDOW(window));
	settle();
	g_object_unref(app);
	g_free(pdf);
	g_free(payload);
	g_free(image);
	return 0;
}
