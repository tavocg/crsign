/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/gui/file_actions.h>
#include <sign/gui/home.h>
#include <sign/gui/validation.h>
#include <sign/history.h>
#include <stdlib.h>

static GMainLoop*      loop;
static SignValidation* view;
static const char*     report;
static const char*     paths[2];
static guint           polls;
static unsigned int    expected_count = 2;

static gboolean poll_result(gpointer data) {
	(void)data;
	if (sign_validation_count(view) == expected_count &&
	    !sign_validation_is_busy(view)) {
		char* count = g_strdup_printf("%u", expected_count);
		g_assert_true(g_file_set_contents(report, count, -1, NULL));
		g_free(count);
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
static GtkWidget* find_named(GtkWidget* widget, const char* name) {
	if (g_str_equal(gtk_widget_get_name(widget), name)) {
		return widget;
	}
	for (GtkWidget* child = gtk_widget_get_first_child(widget); child;
	     child = gtk_widget_get_next_sibling(child)) {
		GtkWidget* found = find_named(child, name);
		if (found) {
			return found;
		}
	}
	return NULL;
}
static void unexpected_open(
    GSimpleAction* action,
    GVariant*      parameter,
    gpointer       data
) {
	(void)action;
	(void)parameter;
	(void)data;
	g_error("A selection or drag unexpectedly opened a file");
}
static gboolean home_ready(gpointer data) {
	GtkWidget* window = data;
	GtkWidget* section = find_named(window, "home-recents");
	GtkWidget* list = gtk_widget_get_first_child(section);
	/* List mode also has a column heading. */
	if (!GTK_IS_FLOW_BOX(list)) {
		list = gtk_widget_get_next_sibling(list);
	}
	GString* positions = g_string_new(NULL);
	for (GtkWidget* child = gtk_widget_get_first_child(list); child;
	     child = gtk_widget_get_next_sibling(child)) {
		GtkWidget*          item = GTK_IS_FLOW_BOX_CHILD(child)
		                               ? gtk_flow_box_child_get_child(
                                    GTK_FLOW_BOX_CHILD(child)
                                )
		                               : child;
		GSimpleActionGroup* actions = g_simple_action_group_new();
		GSimpleAction*      open = g_simple_action_new("open", NULL);
		g_signal_connect(open, "activate", G_CALLBACK(unexpected_open), NULL);
		g_action_map_add_action(G_ACTION_MAP(actions), G_ACTION(open));
		gtk_widget_insert_action_group(
		    item,
		    "document",
		    G_ACTION_GROUP(actions)
		);
		g_object_unref(open);
		g_object_unref(actions);
		graphene_rect_t bounds;
		g_assert_true(gtk_widget_compute_bounds(item, window, &bounds));
		g_string_append_printf(
		    positions,
		    "%d %d\n",
		    (int)(bounds.origin.x + bounds.size.width / 2),
		    (int)(bounds.origin.y + bounds.size.height / 2)
		);
		GListModel* controllers = gtk_widget_observe_controllers(item);
		for (guint n = 0; n < g_list_model_get_n_items(controllers); ++n) {
			GObject* controller = g_list_model_get_item(controllers, n);
			if (GTK_IS_DRAG_SOURCE(controller)) {
				g_signal_connect(
				    controller,
				    "drag-end",
				    G_CALLBACK(drag_end),
				    NULL
				);
			}
			g_object_unref(controller);
		}
		g_object_unref(controllers);
	}
	char* path = g_strconcat(report, ".positions", NULL);
	g_assert_true(g_file_set_contents(path, positions->str, -1, NULL));
	g_free(path);
	g_string_free(positions, TRUE);
	return G_SOURCE_REMOVE;
}
static gboolean receive_files(
    GtkDropTarget* target,
    const GValue*  value,
    double         x,
    double         y,
    gpointer       data
) {
	(void)target;
	(void)x;
	(void)y;
	(void)data;
	GString* received = g_string_new(NULL);
	for (GSList* node = gdk_file_list_get_files(g_value_get_boxed(value)); node;
	     node = node->next) {
		char* path = g_file_get_path(node->data);
		g_assert_nonnull(path);
		g_string_append_printf(received, "%s\n", path);
		g_free(path);
	}
	g_assert_true(g_file_set_contents(report, received->str, -1, NULL));
	g_string_free(received, TRUE);
	g_main_loop_quit(loop);
	return TRUE;
}
int main(int argc, char** argv) {
	g_assert_cmpint(argc, >=, 3);
	adw_init();
	report = argv[2];
	GtkWidget* window = gtk_window_new();
	gtk_window_set_default_size(GTK_WINDOW(window), 400, 400);
	if (g_str_has_prefix(argv[1], "home-")) {
		g_assert_cmpint(argc, ==, 7);
		paths[0] = argv[4];
		paths[1] = argv[5];
		for (int n = argc - 1; n >= 4; --n) {
			g_assert_true(history_record(argv[n], NULL));
		}
		GtkWidget* home = sign_home_new();
		sign_home_set_grid(SIGN_HOME(home), g_str_equal(argv[1], "home-grid"));
		gtk_window_set_child(GTK_WINDOW(window), home);
		gtk_window_set_default_size(GTK_WINDOW(window), 600, 500);
		gtk_window_set_title(GTK_WINDOW(window), "CRSign Drop Source");
		g_timeout_add(250, home_ready, window);
	} else if (g_str_equal(argv[1], "file-target")) {
		GtkWidget*     area = gtk_label_new("Receive actual Home files");
		GtkDropTarget* target = gtk_drop_target_new(
		    GDK_TYPE_FILE_LIST,
		    GDK_ACTION_COPY
		);
		g_signal_connect(target, "drop", G_CALLBACK(receive_files), NULL);
		gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(target));
		gtk_window_set_child(GTK_WINDOW(window), area);
		gtk_window_set_title(GTK_WINDOW(window), "CRSign Drop Target");
	} else if (g_str_equal(argv[1], "source") ||
	           g_str_equal(argv[1], "file-source")) {
		g_assert_cmpint(argc, ==, 6);
		paths[0] = argv[4];
		paths[1] = argv[5];
		GtkWidget*     area = gtk_label_new("Drag these file references");
		GtkDragSource* source = NULL;
		if (g_str_equal(argv[1], "file-source")) {
			GFile* file = g_file_new_for_path(paths[0]);
			sign_file_drag_attach(area, file);
			g_object_unref(file);
			GListModel* controllers = gtk_widget_observe_controllers(area);
			for (guint i = 0; i < g_list_model_get_n_items(controllers); ++i) {
				GtkEventController* controller = g_list_model_get_item(
				    controllers,
				    i
				);
				if (GTK_IS_DRAG_SOURCE(controller)) {
					source = GTK_DRAG_SOURCE(controller);
				}
				g_object_unref(controller);
			}
			g_object_unref(controllers);
			g_assert_nonnull(source);
		} else {
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
			source = gtk_drag_source_new();
			gtk_drag_source_set_actions(source, (GdkDragAction)atoi(argv[3]));
			gtk_drag_source_set_content(source, provider);
			gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(source));
			g_object_unref(provider);
			g_bytes_unref(bytes);
		}
		g_signal_connect(source, "drag-end", G_CALLBACK(drag_end), NULL);
		gtk_window_set_child(GTK_WINDOW(window), area);
		gtk_window_set_title(GTK_WINDOW(window), "CRSign Drop Source");
	} else {
		g_assert_cmpstr(argv[1], ==, "target");
		if (argc == 4) {
			expected_count = (unsigned int)atoi(argv[3]);
		}
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
