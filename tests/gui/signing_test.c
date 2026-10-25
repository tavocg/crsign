/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "helpers.h"
#include <glib/gstdio.h>
#include <math.h>
#include <sign/gui/i18n.h>
#include <sign/gui/pdf_reader.h>
#include <sign/gui/pdf_view.h>
#include <sign/gui/signing.h>
#include <sign/gui/validation.h>

static GtkWidget* window_named(AdwApplication* app, const char* name) {
	for (GList* n = gtk_application_get_windows(GTK_APPLICATION(app)); n;
	     n = n->next) {
		if (!strcmp(gtk_widget_get_name(n->data), name)) {
			return n->data;
		}
	}
	return NULL;
}
static guint handoffs;
static void completed(SignSigning* self, const sign_flow* flow, gpointer data) {
	(void)self;
	GError* error = NULL;
	g_assert_cmpuint(sign_flow_count(flow), ==, 2);
	for (size_t n = 0; n < sign_flow_count(flow); ++n) {
		const sign_flow_item* item = sign_flow_get_item(flow, n);
		g_assert_cmpint(item->operation, ==, SIGN_FLOW_VERIFY);
		g_assert_nonnull(strstr(item->options.input, "-firmado."));
		g_assert_nonnull(item->options.ca_file);
	}
	g_assert_true(sign_validation_add_flow(data, flow, &error));
	g_assert_no_error(error);
	++handoffs;
}
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
	graphene_rect_t page;
	SignPdfReader*  reader = SIGN_PDF_READER(
        find_widget(GTK_WIDGET(self), SIGN_TYPE_PDF_READER)
    );
	g_assert_true(sign_pdf_reader_page_bounds(reader, 1, &page));
	int x = (int)(bounds.origin.x + page.origin.x) + 40,
	    y = (int)(bounds.origin.y + page.origin.y) + 40;
	char* command = drag
	                    ? g_strdup_printf(
	                          "%s mousemove --window %s %d %d mousedown 1 "
	                          "sleep 0.1 "
	                          "mousemove --window %s %d %d sleep 0.1 mouseup 1",
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
	g_assert_true(sign_signing_is_reader(self));
	g_assert_cmpuint(sign_signing_appearance(self, 0)->page_number, ==, 1);
	if (drag) {
		g_assert_cmpfloat(sign_signing_appearance(self, 0)->width, >, 144);
	}
	g_free(command);
	g_free(id);
	g_free(tool);
}
static void zoom_input(GtkWidget* window, SignSigning* self) {
	char* tool = g_find_program_in_path("xdotool");
	if (!tool) {
		return;
	}
	char* id = NULL;
	int   status = 0;
	char* args[] = {tool, "search", "--name", "^CRSign Signing Test$", NULL};
	g_assert_true(g_spawn_sync(
	    NULL,
	    args,
	    NULL,
	    G_SPAWN_DEFAULT,
	    NULL,
	    NULL,
	    &id,
	    NULL,
	    &status,
	    NULL
	));
	g_strstrip(id);
	SignPdfReader* reader = SIGN_PDF_READER(
	    find_widget(GTK_WIDGET(self), SIGN_TYPE_PDF_READER)
	);
	graphene_rect_t before, after, canvas;
	g_assert_true(sign_pdf_reader_page_bounds(reader, 1, &before));
	char* cmd = g_strdup_printf(
	    "%s windowfocus --sync %s key ctrl+plus",
	    tool,
	    id
	);
	g_assert_true(g_spawn_command_line_sync(cmd, NULL, NULL, &status, NULL));
	g_free(cmd);
	settle();
	g_assert_true(sign_pdf_reader_page_bounds(reader, 1, &after));
	g_assert_cmpfloat(after.size.width, >, before.size.width);
	cmd = g_strdup_printf("%s key ctrl+minus", tool);
	g_assert_true(g_spawn_command_line_sync(cmd, NULL, NULL, &status, NULL));
	g_free(cmd);
	settle();
	g_assert_true(sign_pdf_reader_page_bounds(reader, 1, &after));
	g_assert_cmpfloat(fabs(after.size.width - before.size.width), <, .01);
	g_assert_true(gtk_widget_compute_bounds(
	    named(GTK_WIDGET(self), "sign-pdf-scroll"),
	    window,
	    &canvas
	));
	GtkScrolledWindow* scroll = GTK_SCROLLED_WINDOW(
	    named(GTK_WIDGET(self), "sign-pdf-scroll")
	);
	GtkAdjustment* vertical = gtk_scrolled_window_get_vadjustment(scroll);
	int            pointer_x = (int)(before.origin.x + before.size.width / 2),
	    pointer_y = (int)(before.origin.y + before.size.height * .4);
	double original_y = (gtk_adjustment_get_value(vertical) + pointer_y -
	                     before.origin.y) /
	                    before.size.height;
	cmd = g_strdup_printf(
	    "%s mousemove --window %s %d %d keydown ctrl click 4 keyup ctrl",
	    tool,
	    id,
	    (int)canvas.origin.x + pointer_x,
	    (int)canvas.origin.y + pointer_y
	);
	g_assert_true(g_spawn_command_line_sync(cmd, NULL, NULL, &status, NULL));
	g_free(cmd);
	settle();
	g_assert_true(sign_pdf_reader_page_bounds(reader, 1, &after));
	g_assert_cmpfloat(after.size.width, >, before.size.width);
	double zoomed_y = (gtk_adjustment_get_value(vertical) + pointer_y -
	                   after.origin.y) /
	                  after.size.height;
	g_assert_cmpfloat(fabs(zoomed_y - original_y), <, .005);
	cmd = g_strdup_printf("%s keydown ctrl click 5 keyup ctrl", tool);
	g_assert_true(g_spawn_command_line_sync(cmd, NULL, NULL, &status, NULL));
	g_free(cmd);
	settle();
	g_assert_true(sign_pdf_reader_page_bounds(reader, 1, &after));
	g_assert_cmpfloat(fabs(after.size.width - before.size.width), <, .01);
	g_free(id);
	g_free(tool);
}
static void move_stamp(GtkWidget* window, SignSigning* self) {
	char* tool = g_find_program_in_path("xdotool");
	if (!tool) {
		return;
	}
	char* id = NULL;
	int   status = 0;
	char* args[] = {tool, "search", "--name", "^CRSign Signing Test$", NULL};
	g_assert_true(g_spawn_sync(
	    NULL,
	    args,
	    NULL,
	    G_SPAWN_DEFAULT,
	    NULL,
	    NULL,
	    &id,
	    NULL,
	    &status,
	    NULL
	));
	g_strstrip(id);
	SignPdfReader* reader = SIGN_PDF_READER(
	    find_widget(GTK_WIDGET(self), SIGN_TYPE_PDF_READER)
	);
	const libssig_pdf_appearance* a = sign_signing_appearance(self, 0);
	libssig_pdf_appearance        saved = *a;
	graphene_rect_t               bounds, page;
	g_assert_true(sign_pdf_reader_page_bounds(reader, a->page_number, &page));
	g_assert_true(gtk_widget_compute_bounds(
	    named(GTK_WIDGET(self), "sign-pdf-canvas"),
	    window,
	    &bounds
	));
	double zoom = page.size.width / 612.;
	int    x = (int)(bounds.origin.x + page.origin.x +
                  (a->x + a->width / 2) * zoom),
	    y = (int)(bounds.origin.y + page.origin.y +
	              (a->y + a->height / 2) * zoom);
	char* cmd = g_strdup_printf(
	    "%s mousemove --window %s %d %d mousedown 1 sleep 0.1 mousemove "
	    "--window %s %d "
	    "%d sleep 0.1 mouseup 1",
	    tool,
	    id,
	    x,
	    y,
	    id,
	    x + 50,
	    y + 40
	);
	g_assert_true(g_spawn_command_line_sync(cmd, NULL, NULL, &status, NULL));
	g_free(cmd);
	settle();
	g_assert_true(sign_signing_is_reader(self));
	g_assert_cmpfloat(a->x, >, saved.x);
	g_assert_cmpfloat(a->y, >, saved.y);
	g_assert_cmpfloat(a->width, ==, saved.width);
	g_assert_cmpfloat(a->height, ==, saved.height);
	/* Dragging beyond a page edge clamps the origin without resizing. */
	x += 50;
	y += 40;
	cmd = g_strdup_printf(
	    "%s mousemove --window %s %d %d mousedown 1 sleep 0.1 mousemove "
	    "--window %s 1 1 "
	    "sleep 0.1 mouseup 1",
	    tool,
	    id,
	    x,
	    y,
	    id
	);
	g_assert_true(g_spawn_command_line_sync(cmd, NULL, NULL, &status, NULL));
	g_free(cmd);
	settle();
	g_assert_true(sign_signing_is_reader(self));
	g_assert_cmpfloat(a->x, ==, 0);
	g_assert_cmpfloat(a->y, ==, 0);
	g_assert_cmpfloat(a->width, ==, saved.width);
	g_free(id);
	g_free(tool);
}
static void resize_stamp(
    GtkWidget*   window,
    SignSigning* self,
    double       page_width,
    double       page_height
) {
	GtkWidget*     canvas = named(GTK_WIDGET(self), "sign-pdf-canvas");
	SignPdfReader* reader = SIGN_PDF_READER(
	    find_widget(GTK_WIDGET(self), SIGN_TYPE_PDF_READER)
	);
	GtkGestureDrag*           drag = NULL;
	GtkEventControllerMotion* motion = NULL;
	GListModel* controllers = gtk_widget_observe_controllers(canvas);
	for (guint n = 0; n < g_list_model_get_n_items(controllers); ++n) {
		GObject* controller = g_list_model_get_item(controllers, n);
		if (GTK_IS_GESTURE_DRAG(controller)) {
			drag = g_object_ref(GTK_GESTURE_DRAG(controller));
		}
		if (GTK_IS_EVENT_CONTROLLER_MOTION(controller)) {
			motion = g_object_ref(GTK_EVENT_CONTROLLER_MOTION(controller));
		}
		g_object_unref(controller);
	}
	g_object_unref(controllers);
	g_assert_nonnull(drag);
	g_assert_nonnull(motion);
	char* tool = g_find_program_in_path("xdotool");
	char* id = NULL;
	if (tool) {
		char* args[] =
		    {tool, "search", "--name", "^CRSign Signing Test$", NULL};
		int status = 0;
		g_assert_true(g_spawn_sync(
		    NULL,
		    args,
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
	}
	const libssig_pdf_appearance* a = sign_signing_appearance(self, 0);
	GError*                       error = NULL;
	for (unsigned corner = 0; corner < 3; ++corner) {
		g_assert_true(sign_signing_place(self, 100, 140, 180, 90, &error));
		g_assert_no_error(error);
		settle();
		graphene_rect_t page, bounds;
		g_assert_true(
		    sign_pdf_reader_page_bounds(reader, a->page_number, &page)
		);
		g_assert_true(gtk_widget_compute_bounds(canvas, window, &bounds));
		double scale = page.size.width / page_width;
		double sx = corner == 2 ? 1 : -1, sy = corner == 0 ? -1 : 1;
		double x = page.origin.x +
		           (corner == 2 ? a->x + a->width : a->x) * scale;
		double y = page.origin.y +
		           (corner == 0 ? a->y : a->y + a->height) * scale;
		g_signal_emit_by_name(
		    motion,
		    "motion",
		    page.origin.x + (a->x + a->width / 2) * scale,
		    page.origin.y + (a->y + a->height / 2) * scale
		);
		g_assert_cmpstr(
		    gdk_cursor_get_name(gtk_widget_get_cursor(canvas)),
		    ==,
		    "grab"
		);
		g_signal_emit_by_name(
		    motion,
		    "motion",
		    page.origin.x + 500 * scale,
		    page.origin.y + 20 * scale
		);
		g_assert_cmpstr(
		    gdk_cursor_get_name(gtk_widget_get_cursor(canvas)),
		    ==,
		    "crosshair"
		);
		g_signal_emit_by_name(motion, "motion", x, y);
		g_assert_cmpstr(
		    gdk_cursor_get_name(gtk_widget_get_cursor(canvas)),
		    ==,
		    corner == 1 ? "nesw-resize" : "nwse-resize"
		);
		if (tool) {
			char* command = g_strdup_printf(
			    "%s mousemove --window %s %d %d mousedown 1 sleep 0.1 "
			    "mousemove --window "
			    "%s %d %d sleep 0.1 mouseup 1",
			    tool,
			    id,
			    (int)round(bounds.origin.x + x),
			    (int)round(bounds.origin.y + y),
			    id,
			    (int)round(bounds.origin.x + x + sx * 30 * scale),
			    (int)round(bounds.origin.y + y + sy * 15 * scale)
			);
			int status = 0;
			g_assert_true(
			    g_spawn_command_line_sync(command, NULL, NULL, &status, NULL)
			);
			g_assert_cmpint(status, ==, 0);
			g_free(command);
			settle();
			g_assert_true(sign_signing_is_reader(self));
		} else {
			g_signal_emit_by_name(drag, "drag-begin", x, y);
			g_signal_emit_by_name(
			    drag,
			    "drag-update",
			    sx * 30 * scale,
			    sy * 15 * scale
			);
			g_signal_emit_by_name(
			    drag,
			    "drag-end",
			    sx * 30 * scale,
			    sy * 15 * scale
			);
		}
		g_assert_cmpfloat(a->width, >, 180);
		g_assert_cmpfloat(fabs(a->width / a->height - 2), <, .0001);
		g_assert_cmpfloat(
		    fabs((sx < 0 ? a->x + a->width : a->x) - (sx < 0 ? 280 : 100)),
		    <,
		    .001
		);
		g_assert_cmpfloat(
		    fabs((sy < 0 ? a->y + a->height : a->y) - (sy < 0 ? 230 : 140)),
		    <,
		    .001
		);
	}
	/* Extreme drags clamp sizes without flipping or crossing a page edge. */
	for (unsigned shrink = 0; shrink < 2; ++shrink) {
		g_assert_true(sign_signing_place(self, 100, 140, 180, 90, &error));
		graphene_rect_t page;
		g_assert_true(
		    sign_pdf_reader_page_bounds(reader, a->page_number, &page)
		);
		double scale = page.size.width / page_width;
		double delta = (shrink ? -50000 : 50000) * scale;
		g_signal_emit_by_name(
		    drag,
		    "drag-begin",
		    page.origin.x + 280 * scale,
		    page.origin.y + 230 * scale
		);
		g_signal_emit_by_name(drag, "drag-update", delta, delta);
		g_signal_emit_by_name(drag, "drag-end", delta, delta);
		double width = shrink
		                   ? 24
		                   : fmin(
		                         720,
		                         fmin(page_width - 100, (page_height - 140) * 2)
		                     );
		g_assert_cmpfloat(fabs(a->width - width), <, .001);
		g_assert_cmpfloat(fabs(a->height - width / 2), <, .001);
		g_assert_cmpfloat(a->x, ==, 100);
		g_assert_cmpfloat(a->y, ==, 140);
	}
	if (page_width >= 760 && page_height >= 380) {
		g_assert_true(sign_signing_place(self, 20, 20, 180, 90, &error));
		graphene_rect_t page;
		g_assert_true(
		    sign_pdf_reader_page_bounds(reader, a->page_number, &page)
		);
		double scale = page.size.width / page_width;
		g_signal_emit_by_name(
		    drag,
		    "drag-begin",
		    page.origin.x + 200 * scale,
		    page.origin.y + 110 * scale
		);
		g_signal_emit_by_name(
		    drag,
		    "drag-update",
		    50000 * scale,
		    50000 * scale
		);
		g_signal_emit_by_name(drag, "drag-end", 50000 * scale, 50000 * scale);
		g_assert_cmpfloat(a->width, ==, 720);
		g_assert_cmpfloat(a->height, ==, 360);
	}
	g_assert_true(sign_signing_place(self, 100, 140, 180, 90, &error));
	graphene_rect_t page;
	g_assert_true(sign_pdf_reader_page_bounds(reader, a->page_number, &page));
	double scale = page.size.width / page_width;
	double x = page.origin.x + 280 * scale, y = page.origin.y + 230 * scale;
	g_signal_emit_by_name(drag, "drag-begin", x, y);
	g_signal_emit_by_name(drag, "drag-update", 30 * scale, 15 * scale);
	g_signal_emit_by_name(drag, "cancel", NULL);
	g_signal_emit_by_name(motion, "motion", x, y);
	g_assert_cmpstr(
	    gdk_cursor_get_name(gtk_widget_get_cursor(canvas)),
	    ==,
	    "nwse-resize"
	);
	g_assert_cmpfloat(a->width, ==, 180);
	g_signal_emit_by_name(
	    drag,
	    "drag-begin",
	    page.origin.x + 190 * scale,
	    page.origin.y + 185 * scale
	);
	g_assert_cmpstr(
	    gdk_cursor_get_name(gtk_widget_get_cursor(canvas)),
	    ==,
	    "grabbing"
	);
	g_signal_emit_by_name(drag, "cancel", NULL);
	g_signal_emit_by_name(named(GTK_WIDGET(self), "sign-zoom-in"), "clicked");
	settle();
	g_assert_true(sign_pdf_reader_page_bounds(reader, a->page_number, &page));
	scale = page.size.width / page_width;
	g_signal_emit_by_name(
	    drag,
	    "drag-begin",
	    page.origin.x + 280 * scale,
	    page.origin.y + 230 * scale
	);
	g_signal_emit_by_name(drag, "drag-update", 30 * scale, 15 * scale);
	g_signal_emit_by_name(drag, "drag-end", 30 * scale, 15 * scale);
	g_assert_cmpfloat(fabs(a->width - 210), <, .001);
	g_signal_emit_by_name(named(GTK_WIDGET(self), "sign-zoom-out"), "clicked");
	settle();
	sign_flow* flow = sign_signing_build_flow(self, &error);
	g_assert_no_error(error);
	g_assert_cmpfloat(
	    fabs(sign_flow_document(flow, 0)->appearance.width - 210),
	    <,
	    .001
	);
	sign_flow_free(flow);
	g_object_unref(drag);
	g_object_unref(motion);
	g_free(id);
	g_free(tool);
}
int main(int argc, char** argv) {
	g_assert_true(argc == 3 || argc == 4);
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
	if (argc == 4) {
		char* input = g_build_filename(argv[1], "no-token.bin", NULL);
		g_assert_true(g_file_set_contents(input, "test", -1, NULL));
		add(self, input);
		sign_signing_run(self);
		wait_done(self);
		GtkWidget* dialog = window_named(app, "sign-error-dialog");
		g_assert_nonnull(dialog);
		g_assert_false(
		    gtk_widget_get_visible(named(GTK_WIDGET(self), "sign-notice"))
		);
		GtkWidget* label = find_widget(dialog, GTK_TYPE_LABEL);
		g_assert_nonnull(label);
		g_assert_true(g_str_has_prefix(
		    gtk_label_get_text(GTK_LABEL(label)),
		    "No se encontró ningún dispositivo de firma."
		));
		g_assert_null(
		    strstr(gtk_label_get_text(GTK_LABEL(label)), "documents[")
		);
		snapshot(dialog, argv[2], "sign-no-device.png");
		g_signal_emit_by_name(named(dialog, "error-close"), "clicked");
		gtk_window_destroy(GTK_WINDOW(window));
		settle();
		g_object_unref(app);
		g_free(input);
		return 0;
	}
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
	GtkWidget* pdf_reader = find_widget(GTK_WIDGET(self), SIGN_TYPE_PDF_READER);
	GListModel* reader_controllers = gtk_widget_observe_controllers(pdf_reader);
	for (guint i = 0; i < g_list_model_get_n_items(reader_controllers); ++i) {
		GObject* controller = g_list_model_get_item(reader_controllers, i);
		g_assert_false(GTK_IS_DRAG_SOURCE(controller));
		g_object_unref(controller);
	}
	g_object_unref(reader_controllers);
	g_assert_false(
	    gtk_widget_get_visible(named(GTK_WIDGET(self), "sign-profile-image"))
	);
	settle();
	zoom_input(window, self);
	mouse_place(window, self, false);
	sign_signing_remove_appearance(self);
	mouse_place(window, self, true);
	sign_signing_remove_appearance(self);
	sign_signing_set_image(self, image);
	wait_done(self);
	g_assert_true(
	    gtk_widget_get_visible(named(GTK_WIDGET(self), "sign-profile-image"))
	);
	g_assert_true(sign_signing_place(self, 30, 40, 0, 0, &error));
	g_assert_no_error(error);
	const libssig_pdf_appearance* a = sign_signing_appearance(self, 0);
	g_assert_cmpfloat(a->width, ==, 144);
	GtkWidget* information = named(GTK_WIDGET(self), "sign-information");
	g_assert_true(gtk_widget_get_visible(information));
	g_assert_true(gtk_check_button_get_active(GTK_CHECK_BUTTON(information)));
	g_assert_cmpfloat(fabs(a->height - 123.84), <, .01);
	sign_flow* combined = sign_signing_build_flow(self, &error);
	g_assert_no_error(error);
	g_assert_true(sign_flow_document(combined, 0)->appearance_information);
	sign_flow_free(combined);
	snapshot(window, argv[2], "sign-reader-combined.png");
	gtk_check_button_set_active(GTK_CHECK_BUTTON(information), FALSE);
	wait_done(self);
	g_assert_cmpfloat(a->height, ==, 72);
	/* Expanding beyond the page keeps both the old choice and rectangle. */
	g_assert_true(sign_signing_place(self, 30, 700, 144, 72, &error));
	g_assert_no_error(error);
	gtk_check_button_set_active(GTK_CHECK_BUTTON(information), TRUE);
	wait_done(self);
	g_assert_false(gtk_check_button_get_active(GTK_CHECK_BUTTON(information)));
	g_assert_cmpfloat(a->height, ==, 72);
	g_assert_cmpfloat(a->y, ==, 700);
	g_assert_true(sign_signing_place(self, 30, 40, 144, 72, &error));
	g_assert_no_error(error);
	move_stamp(window, self);
	resize_stamp(window, self, 612, 792);
	g_assert_true(sign_signing_place(self, 30, 40, 1, 1, &error));
	g_assert_cmpfloat(a->width, ==, 24);
	g_assert_cmpfloat(a->height, ==, 12);
	g_assert_false(sign_signing_place(self, 611, 791, 0, 0, &error));
	g_clear_error(&error);
	sign_signing_set_page(self, 2);
	wait_done(self);
	g_assert_true(sign_signing_place(self, 40, 50, 300, 150, &error));
	g_assert_cmpuint(a->page_number, ==, 2);
	resize_stamp(window, self, 800, 480);
	g_assert_true(sign_signing_place(self, 40, 50, 300, 150, &error));
	gtk_window_set_default_size(GTK_WINDOW(window), 420, 760);
	settle();
	g_assert_cmpint(gtk_widget_get_width(window), <=, 420);
	snapshot(window, argv[2], "sign-reader-narrow.png");
	gtk_window_set_default_size(GTK_WINDOW(window), 820, 760);
	snapshot(window, argv[2], "sign-reader.png");
	SignPdfReader* reader = SIGN_PDF_READER(
	    find_widget(GTK_WIDGET(self), SIGN_TYPE_PDF_READER)
	);
	GtkScrolledWindow* scroll = GTK_SCROLLED_WINDOW(
	    named(GTK_WIDGET(self), "sign-pdf-scroll")
	);
	GtkAdjustment*  adjustment = gtk_scrolled_window_get_vadjustment(scroll);
	graphene_rect_t first, before, after;
	g_assert_true(sign_pdf_reader_page_bounds(reader, 1, &first));
	g_assert_cmpfloat(
	    first.size.height,
	    <=,
	    gtk_widget_get_height(GTK_WIDGET(scroll))
	);
	g_assert_true(sign_pdf_reader_page_bounds(reader, 2, &before));
	libssig_pdf_appearance saved = *a;
	g_signal_emit_by_name(named(GTK_WIDGET(self), "sign-zoom-in"), "clicked");
	settle();
	g_assert_true(sign_pdf_reader_page_bounds(reader, 2, &after));
	g_assert_cmpfloat(after.size.width, >, before.size.width);
	g_assert_cmpfloat(a->x, ==, saved.x);
	g_assert_cmpfloat(a->width, ==, saved.width);
	g_signal_emit_by_name(named(GTK_WIDGET(self), "sign-zoom-out"), "clicked");
	settle();
	g_assert_true(sign_pdf_reader_page_bounds(reader, 2, &after));
	g_assert_cmpfloat(fabs(after.size.width - before.size.width), <, .01);
	gtk_adjustment_set_value(adjustment, 0);
	settle();
	g_assert_cmpint(
	    gtk_spin_button_get_value_as_int(
	        GTK_SPIN_BUTTON(named(GTK_WIDGET(self), "sign-page"))
	    ),
	    ==,
	    1
	);
	gtk_adjustment_set_value(adjustment, gtk_adjustment_get_upper(adjustment));
	settle();
	g_assert_cmpint(
	    gtk_spin_button_get_value_as_int(
	        GTK_SPIN_BUTTON(named(GTK_WIDGET(self), "sign-page"))
	    ),
	    ==,
	    2
	);
	g_assert_true(gtk_widget_get_visible(
	    named(GTK_WIDGET(self), "sign-remove-appearance")
	));
	snapshot(window, argv[2], "sign-continuous.png");
	GtkWidget* remove = named(GTK_WIDGET(self), "sign-remove-appearance");
	g_assert_cmpint(
	    gtk_widget_get_width(remove),
	    ==,
	    gtk_widget_get_height(remove)
	);
	adw_style_manager_set_color_scheme(
	    adw_style_manager_get_default(),
	    ADW_COLOR_SCHEME_FORCE_DARK
	);
	snapshot(window, argv[2], "sign-continuous-dark.png");
	g_assert_cmpint(
	    gtk_widget_get_width(remove),
	    ==,
	    gtk_widget_get_height(remove)
	);

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
	GtkListBox* queue = GTK_LIST_BOX(named(GTK_WIDGET(self), "sign-documents"));
	if (!queue) {
		queue = GTK_LIST_BOX(gtk_widget_get_ancestor(
		    named(GTK_WIDGET(self), "sign-change-output"),
		    GTK_TYPE_LIST_BOX
		));
	}
	GtkListBoxRow* plain = gtk_list_box_get_row_at_index(queue, 1);
	g_assert_null(named(GTK_WIDGET(plain), "sign-graphical-signature"));
	GtkWidget* graphical = named(
	    GTK_WIDGET(gtk_list_box_get_row_at_index(queue, 0)),
	    "sign-graphical-signature"
	);
	g_assert_nonnull(graphical);
	g_assert_cmpstr(
	    gtk_button_get_icon_name(GTK_BUTTON(graphical)),
	    ==,
	    "insert-image-symbolic"
	);
	g_assert_cmpstr(
	    gtk_button_get_icon_name(
	        GTK_BUTTON(named(GTK_WIDGET(self), "sign-image"))
	    ),
	    ==,
	    "image-x-generic-symbolic"
	);

	g_signal_emit_by_name(graphical, "clicked");
	g_assert_true(sign_signing_is_reader(self));
	wait_done(self);
	sign_signing_show_queue(self);

	g_assert_false(gtk_widget_get_visible(
	    named(GTK_WIDGET(plain), "sign-document-warning")
	));
	char* old_notice = g_strdup(
	    gtk_label_get_text(GTK_LABEL(named(GTK_WIDGET(self), "sign-notice")))
	);
	sign_signing_open_document(self, 1);
	g_assert_false(sign_signing_is_reader(self));
	g_assert_cmpstr(
	    gtk_label_get_text(GTK_LABEL(named(GTK_WIDGET(self), "sign-notice"))),
	    ==,
	    old_notice
	);
	g_free(old_notice);

	g_assert_nonnull(strstr(sign_signing_output(self, 0), "-firmado.pdf"));
	g_assert_nonnull(strstr(sign_signing_output(self, 1), "-firmado.bin"));
	g_assert_null(named(GTK_WIDGET(self), "sign-output-location"));
	GtkWidget* output_options = named(GTK_WIDGET(self), "sign-output-options");
	g_assert_nonnull(output_options);
	GtkWidget* sign_button = named(GTK_WIDGET(self), "sign-run");
	g_assert_nonnull(sign_button);
	g_assert_true(
	    gtk_widget_get_parent(output_options) ==
	    gtk_widget_get_parent(sign_button)
	);
	g_assert_true(gtk_widget_get_next_sibling(sign_button) == output_options);
	GtkWidget* output_button = named(GTK_WIDGET(self), "sign-change-output");
	g_assert_nonnull(output_button);
	g_assert_cmpstr(
	    gtk_button_get_icon_name(GTK_BUTTON(output_button)),
	    ==,
	    "document-edit-symbolic"
	);
	GtkWidget* output_row = gtk_widget_get_ancestor(
	    output_button,
	    ADW_TYPE_ACTION_ROW
	);
	g_assert_nonnull(output_row);
	g_assert_nonnull(
	    g_object_get_data(G_OBJECT(output_row), "crsign-file-actions")
	);
	g_assert_null(named(GTK_WIDGET(self), "document-location"));
	char* first_output = g_strdup(sign_signing_output(self, 0));
	char* second_output = g_strdup(sign_signing_output(self, 1));
	sign_signing_set_output(self, 1, first_output);
	sign_signing_run(self);
	GtkWidget* conflict = window_named(app, "sign-conflict-dialog");
	g_assert_nonnull(conflict);
	g_assert_false(
	    gtk_widget_get_sensitive(named(conflict, "sign-conflict-overwrite"))
	);
	g_signal_emit_by_name(named(conflict, "sign-conflict-cancel"), "clicked");
	settle();
	g_assert_null(window_named(app, "sign-conflict-dialog"));
	sign_signing_set_output(self, 1, second_output);
	g_free(first_output);
	g_free(second_output);
	g_assert_false(
	    gtk_widget_get_visible(named(GTK_WIDGET(self), "sign-warning"))
	);
	GtkDropDown* profiles = GTK_DROP_DOWN(sign_signing_profile_selector(self));
	gtk_drop_down_set_selected(profiles, 1);
	sign_signing_open_document(self, 0);
	wait_done(self);
	gtk_check_button_set_active(GTK_CHECK_BUTTON(information), TRUE);
	wait_done(self);
	g_assert_cmpfloat(fabs(a->height - 123.84), <, .01);
	sign_signing_show_queue(self);

	sign_flow* flow = sign_signing_build_flow(self, &error);
	g_assert_no_error(error);
	g_assert_nonnull(flow);
	g_assert_cmpuint(sign_flow_count(flow), ==, 2);
	g_assert_true(sign_flow_document(flow, 0)->appearance_information);
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
	g_assert_cmpstr(
	    gtk_label_get_text(GTK_LABEL(named(GTK_WIDGET(self), "sign-total"))),
	    ==,
	    "Total: 2"
	);
	GtkWidget* remove_document = named(
	    GTK_WIDGET(self),
	    "sign-remove-document"
	);
	g_assert_cmpstr(
	    gtk_widget_get_name(gtk_widget_get_prev_sibling(remove_document)),
	    ==,
	    "sign-change-output"
	);
	SignValidation* validation = SIGN_VALIDATION(sign_validation_new());
	gtk_widget_set_visible(GTK_WIDGET(validation), FALSE);
	gtk_box_append(GTK_BOX(box), GTK_WIDGET(validation));
	g_signal_connect(
	    self,
	    "signing-completed",
	    G_CALLBACK(completed),
	    validation
	);
	snapshot(window, argv[2], "sign-queue.png");
	guint timer = g_timeout_add(10, answer_pin, app);
	sign_signing_run(self);
	g_assert_false(
	    gtk_widget_get_sensitive(named(GTK_WIDGET(self), "sign-clear"))
	);
	wait_done(self);
	g_source_remove(timer);
	g_assert_cmpuint(handoffs, ==, 1);
	gint64 deadline = g_get_monotonic_time() + 15 * G_TIME_SPAN_SECOND;
	while (sign_validation_is_busy(validation) &&
	       g_get_monotonic_time() < deadline) {
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	}
	g_assert_false(sign_validation_is_busy(validation));
	g_assert_cmpuint(sign_validation_count(validation), ==, 2);
	for (size_t n = 0; n < 2; ++n) {
		const sign_flow_document_result* result = sign_validation_result(
		    validation,
		    n
		);
		g_assert_true(result->ok);
		g_assert_true(result->verification.signature_valid);
		g_assert_true(result->verification.trusted);
	}
	g_assert_cmpuint(pins, ==, 1);
	char* output = g_build_filename(argv[1], "page-firmado.pdf", NULL);
	g_assert_true(g_file_test(output, G_FILE_TEST_IS_REGULAR));
	g_free(output);
	output = g_build_filename(argv[1], "payload-firmado.bin", NULL);
	g_assert_true(g_file_test(output, G_FILE_TEST_IS_REGULAR));
	g_free(output);
	/* Existing outputs are reported together before a second PIN request. */
	const char* warning = gtk_widget_get_tooltip_text(
	    named(GTK_WIDGET(self), "sign-warning")
	);
	g_assert_nonnull(warning);
	g_assert_nonnull(strstr(warning, _("Some output files already exist")));
	sign_signing_run(self);
	conflict = window_named(app, "sign-conflict-dialog");
	g_assert_nonnull(conflict);
	g_assert_true(gtk_widget_get_visible(conflict));
	g_signal_emit_by_name(named(conflict, "sign-conflict-cancel"), "clicked");
	settle();
	g_assert_null(window_named(app, "sign-conflict-dialog"));
	g_assert_cmpuint(pins, ==, 1);
	g_assert_cmpuint(handoffs, ==, 1);
	/* Overwrite proceeds after the same confirmation. */
	timer = g_timeout_add(10, answer_pin, app);
	sign_signing_run(self);
	conflict = window_named(app, "sign-conflict-dialog");
	g_assert_nonnull(conflict);
	g_signal_emit_by_name(
	    named(conflict, "sign-conflict-overwrite"),
	    "clicked"
	);
	wait_done(self);
	g_source_remove(timer);
	g_assert_cmpuint(pins, ==, 2);
	g_assert_cmpuint(handoffs, ==, 2);
	deadline = g_get_monotonic_time() + 15 * G_TIME_SPAN_SECOND;
	while (sign_validation_is_busy(validation) &&
	       g_get_monotonic_time() < deadline) {
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	}
	g_assert_false(sign_validation_is_busy(validation));
	g_assert_cmpuint(sign_validation_count(validation), ==, 4);
	g_signal_emit_by_name(named(GTK_WIDGET(self), "sign-clear"), "clicked");
	g_assert_cmpuint(sign_signing_count(self), ==, 0);
	g_assert_cmpstr(
	    gtk_label_get_text(GTK_LABEL(named(GTK_WIDGET(self), "sign-total"))),
	    ==,
	    "Total: 0"
	);
	/* Clearing during preview prevents a stale callback restoring a document.
	 */
	GListStore* pending = g_list_store_new(G_TYPE_FILE);
	GFile*      pending_file = g_file_new_for_path(pdf);
	g_list_store_append(pending, pending_file);
	sign_signing_add_files(self, G_LIST_MODEL(pending));
	sign_signing_clear(self);
	settle();
	g_assert_false(sign_signing_is_reader(self));
	g_assert_false(sign_signing_is_loading(self));
	g_assert_cmpuint(sign_signing_count(self), ==, 0);
	g_object_unref(pending_file);
	g_object_unref(pending);
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
	g_assert_cmpuint(pins, ==, 3);
	output = g_build_filename(argv[1], "cancel-firmado.bin", NULL);
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
	g_assert_cmpuint(pins, ==, 4);
	g_object_unref(cancelled);
	gtk_window_destroy(GTK_WINDOW(window));
	settle();
	g_object_unref(app);
	g_free(pdf);
	g_free(payload);
	g_free(image);
	return 0;
}
