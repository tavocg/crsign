/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_GUI_TEST_HELPERS_H
#define SIGN_GUI_TEST_HELPERS_H
#include <adwaita.h>
#include <string.h>
static void settle(void) {
	gint64 deadline = g_get_monotonic_time() + 250 * G_TIME_SPAN_MILLISECOND;
	do {
		while (g_main_context_iteration(NULL, FALSE)) {
		}
		g_usleep(1000);
	} while (g_get_monotonic_time() < deadline);
}

/* Compare arrow shapes independently of accent/foreground color changes. */
static inline GBytes* arrow_shape(GtkWidget* window, GtkWidget* row) {
	settle();
	GtkWidget* arrow = NULL;
	GQueue     queue = G_QUEUE_INIT;
	g_queue_push_tail(&queue, row);
	while (!g_queue_is_empty(&queue)) {
		GtkWidget* current = g_queue_pop_head(&queue);
		if (gtk_widget_has_css_class(current, "expander-row-arrow")) {
			arrow = current;
			g_queue_clear(&queue);
			break;
		}
		for (GtkWidget* child = gtk_widget_get_first_child(current); child;
		     child = gtk_widget_get_next_sibling(child)) {
			g_queue_push_tail(&queue, child);
		}
	}
	g_assert_nonnull(arrow);
	int width = gtk_widget_get_width(arrow),
	    height = gtk_widget_get_height(arrow);
	GdkPaintable* paintable = gtk_widget_paintable_new(arrow);
	GtkSnapshot*  snapshot = gtk_snapshot_new();
	gdk_paintable_snapshot(paintable, GDK_SNAPSHOT(snapshot), width, height);
	GskRenderNode* node = gtk_snapshot_free_to_node(snapshot);
	g_assert_nonnull(node);
	graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, width, height);
	GdkTexture*     texture = gsk_renderer_render_texture(
        gtk_native_get_renderer(GTK_NATIVE(window)),
        node,
        &bounds
    );
	gsize   size = (gsize)width * height;
	guchar* pixels = g_malloc(size * 4);
	guchar* alpha = g_malloc(size);
	gdk_texture_download(texture, pixels, (gsize)width * 4);
	for (gsize n = 0; n < size; ++n) {
		alpha[n] = pixels[4 * n + 3];
	}
	g_free(pixels);
	g_object_unref(texture);
	gsk_render_node_unref(node);
	g_object_unref(paintable);
	return g_bytes_new_take(alpha, size);
}

static inline GtkWidget* find_widget(GtkWidget* root, GType type) {
	if (g_type_is_a(G_OBJECT_TYPE(root), type)) {
		return root;
	}
	for (GtkWidget* child = gtk_widget_get_first_child(root); child;
	     child = gtk_widget_get_next_sibling(child)) {
		GtkWidget* result = find_widget(child, type);
		if (result) {
			return result;
		}
	}
	return NULL;
}

static GtkWidget* named(GtkWidget* root, const char* name) {
	if (!strcmp(gtk_widget_get_name(root), name)) {
		return root;
	}
	for (GtkWidget* child = gtk_widget_get_first_child(root); child;
	     child = gtk_widget_get_next_sibling(child)) {
		GtkWidget* found = named(child, name);
		if (found) {
			return found;
		}
	}
	return NULL;
}
static void snapshot(
    GtkWidget*  window,
    const char* directory,
    const char* name
) {
	if (!directory) {
		return;
	}
	settle();
	int width = gtk_widget_get_width(window),
	    height = gtk_widget_get_height(window);
	GdkPaintable* paintable = gtk_widget_paintable_new(window);
	GtkSnapshot*  snapshot = gtk_snapshot_new();
	gdk_paintable_snapshot(paintable, GDK_SNAPSHOT(snapshot), width, height);
	GskRenderNode* node = gtk_snapshot_free_to_node(snapshot);
	g_assert_nonnull(node);
	graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, width, height);
	GdkTexture*     texture = gsk_renderer_render_texture(
        gtk_native_get_renderer(GTK_NATIVE(window)),
        node,
        &bounds
    );
	char* path = g_build_filename(directory, name, NULL);
	g_assert_true(gdk_texture_save_to_png(texture, path));
	g_free(path);
	g_object_unref(texture);
	gsk_render_node_unref(node);
	g_object_unref(paintable);
}

#endif
