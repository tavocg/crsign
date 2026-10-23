/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/gui/validation.h>
#include <stdlib.h>

static GMainLoop*      loop;
static SignValidation* view;
static const char*     report;
static const char*     paths[2];
static guint           polls;

static gboolean poll_result(gpointer data) {
	(void)data;
	if (sign_validation_count(view) == 2 && !sign_validation_is_busy(view)) {
		g_assert_true(g_file_set_contents(report, "2", -1, NULL));
		g_main_loop_quit(loop);
		return G_SOURCE_REMOVE;
	}
	g_assert_cmpuint(++polls, <, 1500);
	return G_SOURCE_CONTINUE;
}
static void drag_end(
    GtkDragSource* source,
    GdkDrag*       drag,
    gboolean       delete_data,
    gpointer       data
) {
	(void)source;
	(void)drag;
	(void)data;
	/* Emulate an owner that honours MOVE by deleting its originals. */
	if (delete_data) {
		for (size_t n = 0; n < G_N_ELEMENTS(paths); ++n) {
			GFile* file = g_file_new_for_path(paths[n]);
			g_assert_true(g_file_delete(file, NULL, NULL));
			g_object_unref(file);
		}
	}
	g_assert_true(
	    g_file_set_contents(report, delete_data ? "delete" : "keep", -1, NULL)
	);
	g_main_loop_quit(loop);
}
int main(int argc, char** argv) {
	g_assert_cmpint(argc, >=, 3);
	adw_init();
	report = argv[2];
	GtkWidget* window = gtk_window_new();
	gtk_window_set_default_size(GTK_WINDOW(window), 400, 400);
	if (g_str_equal(argv[1], "source")) {
		g_assert_cmpint(argc, ==, 6);
		paths[0] = argv[4];
		paths[1] = argv[5];
		GString* uris = g_string_new(NULL);
		for (size_t n = 0; n < G_N_ELEMENTS(paths); ++n) {
			char* uri = g_filename_to_uri(paths[n], NULL, NULL);
			g_assert_nonnull(uri);
			g_string_append_printf(uris, "%s\r\n", uri);
			g_free(uri);
		}
		GBytes*             bytes = g_string_free_to_bytes(uris);
		GdkContentProvider* provider = gdk_content_provider_new_for_bytes(
		    "text/uri-list",
		    bytes
		);
		GtkDragSource* source = gtk_drag_source_new();
		gtk_drag_source_set_actions(source, (GdkDragAction)atoi(argv[3]));
		gtk_drag_source_set_content(source, provider);
		g_signal_connect(source, "drag-end", G_CALLBACK(drag_end), NULL);
		GtkWidget* area = gtk_label_new("Drag these file references");
		gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(source));
		gtk_window_set_child(GTK_WINDOW(window), area);
		gtk_window_set_title(GTK_WINDOW(window), "CRSign Drop Source");
		g_object_unref(provider);
		g_bytes_unref(bytes);
	} else {
		g_assert_cmpstr(argv[1], ==, "target");
		view = SIGN_VALIDATION(sign_validation_new());
		gtk_window_set_child(GTK_WINDOW(window), GTK_WIDGET(view));
		gtk_window_set_title(GTK_WINDOW(window), "CRSign Drop Target");
		g_timeout_add(10, poll_result, NULL);
	}
	gtk_window_present(GTK_WINDOW(window));
	loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(loop);
	gtk_window_destroy(GTK_WINDOW(window));
	g_main_loop_unref(loop);
	return 0;
}
