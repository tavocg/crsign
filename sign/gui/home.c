/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/gui/error.h>
#include <sign/gui/file_actions.h>
#include <sign/gui/home.h>
#include <sign/gui/i18n.h>
#include <sign/gui/pdf_view.h>
#include <sign/history.h>
#include <sign/ui_preferences.h>
#include <stdlib.h>
#include <string.h>
#define PAGE_SIZE 25
typedef struct {
	GtkWidget* list;
} home_section;
struct _SignHome {
	GtkBox        parent_instance;
	sign_history* history;
	GtkWidget *   favorites, *recents, *favorite_more, *recent_more, *notice;
	GtkWidget *   favorite_heading, *recent_heading;
	GtkWidget *   search_button, *search_entry, *search_rows, *search_more;
	size_t        favorite_limit, recent_limit, search_limit;
	bool          grid, closing;
	guint         timer;
	GFileMonitor* monitor;
	GCancellable* thumbnails;
	GHashTable*   selected;
	GtkWidget*    selection_anchor;
	GtkGesture*   outside_click;
	bool          dragging;
};
G_DEFINE_TYPE(SignHome, sign_home, GTK_TYPE_BOX)
static guint      history_changed;
static void       render_home(SignHome* self);
static void       search_update(SignHome* self);
static GtkWindow* parent_window(SignHome* self) {
	GtkRoot* root = gtk_widget_get_root(GTK_WIDGET(self));
	return GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : NULL;
}
static void favorite_changed(GFile* file, bool favorite, gpointer data) {
	SignHome* self = data;
	char*     path = g_file_get_path(file);
	GError*   error = NULL;
	if (!history_favorite(path, favorite, &error)) {
		gui_error_dialog(
		    parent_window(self),
		    _("Could not update favorites"),
		    _("The document history could not be saved. Check available space, "
		      "file permissions and the history size limit.")
		);
		g_clear_error(&error);
	}
	g_free(path);
	sign_home_reload(self);
}
static char* date_text(gint64 time) {
	GDateTime*  utc = g_date_time_new_from_unix_utc(time / G_USEC_PER_SEC);
	GDateTime*  local = utc ? g_date_time_to_local(utc) : NULL;
	GDateTime*  now = g_date_time_new_now_local();
	GDateTime*  yesterday = g_date_time_add_days(now, -1);
	const char* format = _("%e %b %Y");
	if (local) {
		int day = g_date_time_get_day_of_year(local);
		int year = g_date_time_get_year(local);
		if (year == g_date_time_get_year(now) &&
		    day == g_date_time_get_day_of_year(now)) {
			format = _("Today · %H:%M");
		} else if (year == g_date_time_get_year(yesterday) &&
		           day == g_date_time_get_day_of_year(yesterday)) {
			format = _("Yesterday · %H:%M");
		}
	}
	char* text = local ? g_date_time_format(local, format)
	                   : g_strdup(_("Unknown date"));
	g_date_time_unref(yesterday);
	g_date_time_unref(now);
	if (local) {
		g_date_time_unref(local);
	}
	if (utc) {
		g_date_time_unref(utc);
	}
	return text;
}
static void clear(GtkWidget* widget) {
	GtkWidget* child;
	while ((child = gtk_widget_get_first_child(widget))) {
		if (GTK_IS_FLOW_BOX(widget)) {
			gtk_flow_box_remove(GTK_FLOW_BOX(widget), child);
		} else if (GTK_IS_LIST_BOX(widget)) {
			gtk_list_box_remove(GTK_LIST_BOX(widget), child);
		} else {
			gtk_box_remove(GTK_BOX(widget), child);
		}
	}
}
static void selection_clear(SignHome* self) {
	GHashTableIter iter;
	gpointer       item;
	g_hash_table_iter_init(&iter, self->selected);
	while (g_hash_table_iter_next(&iter, &item, NULL)) {
		gtk_widget_remove_css_class(GTK_WIDGET(item), "selected");
	}
	g_hash_table_remove_all(self->selected);
}
static void selection_set(SignHome* self, GtkWidget* item, bool selected) {
	bool current = g_hash_table_contains(self->selected, item);
	if (current == selected) {
		return;
	}
	if (selected) {
		g_hash_table_add(self->selected, item);
		gtk_widget_add_css_class(item, "selected");
	} else {
		g_hash_table_remove(self->selected, item);
		gtk_widget_remove_css_class(item, "selected");
	}
}
static bool item_selected(SignHome* self, GtkWidget* item) {
	return g_hash_table_contains(self->selected, item);
}
static GtkWidget* section_item(GtkWidget* child) {
	return GTK_IS_FLOW_BOX_CHILD(child)
	           ? gtk_flow_box_child_get_child(GTK_FLOW_BOX_CHILD(child))
	           : child;
}
static home_section* item_section(GtkWidget* item) {
	for (GtkWidget* widget = item; widget;
	     widget = gtk_widget_get_parent(widget)) {
		home_section* section = g_object_get_data(
		    G_OBJECT(widget),
		    "home-section"
		);
		if (section) {
			return section;
		}
	}
	return NULL;
}
static int item_index(home_section* section, GtkWidget* item) {
	int index = 0;
	for (GtkWidget* child = gtk_widget_get_first_child(section->list); child;
	     child = gtk_widget_get_next_sibling(child), ++index) {
		if (section_item(child) == item) {
			return index;
		}
	}
	return -1;
}
static void select_range(
    SignHome*     self,
    home_section* section,
    GtkWidget*    anchor,
    GtkWidget*    item
) {
	int first = item_index(section, anchor);
	int last = item_index(section, item);
	if (first < 0 || last < 0) {
		return;
	}
	if (first > last) {
		int swap = first;
		first = last;
		last = swap;
	}
	int index = 0;
	for (GtkWidget* child = gtk_widget_get_first_child(section->list); child;
	     child = gtk_widget_get_next_sibling(child), ++index) {
		if (index < first || index > last) {
			continue;
		}
		GtkWidget* entry = section_item(child);
		if (entry) {
			selection_set(self, entry, true);
		}
	}
}
static bool picked_button(GtkWidget* item, double x, double y) {
	GtkWidget* picked = gtk_widget_pick(item, x, y, GTK_PICK_DEFAULT);
	for (GtkWidget* widget = picked; widget && widget != item;
	     widget = gtk_widget_get_parent(widget)) {
		if (GTK_IS_BUTTON(widget)) {
			return true;
		}
	}
	return false;
}
static void item_pressed(
    GtkGestureClick* gesture,
    int              presses,
    double           x,
    double           y,
    gpointer         data
) {
	SignHome*  self = data;
	GtkWidget* item = gtk_event_controller_get_widget(
	    GTK_EVENT_CONTROLLER(gesture)
	);
	if (picked_button(item, x, y)) {
		return;
	}
	/* Own primary clicks together with the drag source, so the container
	 * cannot open a file on the second press before a drag can begin. */
	gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
	if (presses != 1) {
		return;
	}
	GdkModifierType state = gtk_event_controller_get_current_event_state(
	    GTK_EVENT_CONTROLLER(gesture)
	);
	bool control = (state & GDK_CONTROL_MASK) != 0;
	bool shift = (state & GDK_SHIFT_MASK) != 0;
	if (shift && self->selection_anchor) {
		home_section* section = item_section(item);
		if (section && item_section(self->selection_anchor) == section) {
			if (!control) {
				selection_clear(self);
			}
			select_range(self, section, self->selection_anchor, item);
			return;
		}
	}
	if (control) {
		selection_set(self, item, !item_selected(self, item));
		self->selection_anchor = item;
		return;
	}
	/* Preserve an existing group until release so dragging any selected item
	 * exports the whole group. An unselected press replaces it immediately. */
	if (!item_selected(self, item)) {
		selection_clear(self);
		selection_set(self, item, true);
		self->selection_anchor = item;
	}
}
static void item_released(
    GtkGestureClick* gesture,
    int              presses,
    double           x,
    double           y,
    gpointer         data
) {
	SignHome*  self = data;
	GtkWidget* item = gtk_event_controller_get_widget(
	    GTK_EVENT_CONTROLLER(gesture)
	);
	GdkModifierType state = gtk_event_controller_get_current_event_state(
	    GTK_EVENT_CONTROLLER(gesture)
	);
	if (self->dragging || picked_button(item, x, y) ||
	    (state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK))) {
		return;
	}
	if (presses == 2) {
		gtk_widget_activate_action(item, "document.open", NULL);
		return;
	}
	if (presses != 1) {
		return;
	}
	selection_clear(self);
	selection_set(self, item, true);
	self->selection_anchor = item;
}
static void outside_pressed(
    GtkGestureClick* gesture,
    int              presses,
    double           x,
    double           y,
    gpointer         data
) {
	(void)presses;
	SignHome* self = data;
	self->dragging = false;
	GtkWidget* root = gtk_event_controller_get_widget(
	    GTK_EVENT_CONTROLLER(gesture)
	);
	GtkWidget* picked = gtk_widget_pick(root, x, y, GTK_PICK_DEFAULT);
	for (GtkWidget* widget = picked; widget;
	     widget = gtk_widget_get_parent(widget)) {
		if (g_object_get_data(G_OBJECT(widget), "home-item") &&
		    gtk_widget_is_ancestor(widget, GTK_WIDGET(self))) {
			return;
		}
	}
	selection_clear(self);
	self->selection_anchor = NULL;
}
static GdkContentProvider* home_drag_prepare(
    GtkDragSource* source,
    double         x,
    double         y,
    gpointer       data
) {
	(void)x;
	(void)y;
	SignHome*  self = data;
	GtkWidget* item = g_object_get_data(G_OBJECT(source), "home-item");
	self->dragging = true;
	if (item && !item_selected(self, item)) {
		selection_clear(self);
		selection_set(self, item, true);
		self->selection_anchor = item;
	}
	GHashTable*
	    seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	GPtrArray* paths = g_ptr_array_new_with_free_func(g_free);
	if (item && item_selected(self, item)) {
		GHashTableIter iter;
		gpointer       selected;
		g_hash_table_iter_init(&iter, self->selected);
		while (g_hash_table_iter_next(&iter, &selected, NULL)) {
			const char* path = g_object_get_data(
			    G_OBJECT(selected),
			    "home-path"
			);
			if (path && !g_hash_table_contains(seen, path)) {
				g_hash_table_add(seen, g_strdup(path));
				g_ptr_array_add(paths, g_strdup(path));
			}
		}
	} else if (item) {
		const char* path = g_object_get_data(G_OBJECT(item), "home-path");
		if (path) {
			g_hash_table_add(seen, g_strdup(path));
			g_ptr_array_add(paths, g_strdup(path));
		}
	}
	GdkContentProvider* content = NULL;
	if (paths->len) {
		GFile** files = g_new0(GFile*, paths->len);
		for (guint n = 0; n < paths->len; ++n) {
			files[n] = g_file_new_for_path(g_ptr_array_index(paths, n));
		}
		GValue value = G_VALUE_INIT;
		g_value_init(&value, GDK_TYPE_FILE_LIST);
		g_value_take_boxed(
		    &value,
		    gdk_file_list_new_from_array(files, paths->len)
		);
		content = gdk_content_provider_new_for_value(&value);
		g_value_unset(&value);
		for (guint n = 0; n < paths->len; ++n) {
			g_object_unref(files[n]);
		}
		g_free(files);
	}
	g_ptr_array_unref(paths);
	g_hash_table_unref(seen);
	return content;
}
typedef struct {
	char*            path;
	cairo_surface_t* surface;
} thumbnail_job;
static void thumbnail_free(gpointer data) {
	thumbnail_job* job = data;
	g_free(job->path);
	if (job->surface) {
		cairo_surface_destroy(job->surface);
	}
	g_free(job);
}
static void thumbnail_worker(
    GTask*        task,
    gpointer      source,
    gpointer      data,
    GCancellable* cancel
) {
	(void)source;
	thumbnail_job*     job = data;
	GFile*             file = g_file_new_for_path(job->path);
	GError*            error = NULL;
	sign_pdf_document* pdf = sign_pdf_open(file, cancel, &error);
	g_object_unref(file);
	if (pdf) {
		job->surface = sign_pdf_thumbnail(pdf, cancel, &error);
		sign_pdf_unref(pdf);
	}
	g_clear_error(&error);
	g_task_return_boolean(task, job->surface != NULL);
}
static void thumbnail_finished(
    GObject*      source,
    GAsyncResult* result,
    gpointer      data
) {
	(void)data;
	GError* error = NULL;
	bool    ok = g_task_propagate_boolean(G_TASK(result), &error);
	g_clear_error(&error);
	if (!ok || !gtk_widget_get_root(GTK_WIDGET(source))) {
		return;
	}
	thumbnail_job* job = g_task_get_task_data(G_TASK(result));
	int            width = cairo_image_surface_get_width(job->surface),
	    height = cairo_image_surface_get_height(job->surface),
	    stride = cairo_image_surface_get_stride(job->surface);
	cairo_surface_flush(job->surface);
	GBytes* bytes = g_bytes_new(
	    cairo_image_surface_get_data(job->surface),
	    (gsize)stride * height
	);
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
	GdkMemoryFormat format = GDK_MEMORY_B8G8R8A8_PREMULTIPLIED;
#else
	GdkMemoryFormat format = GDK_MEMORY_A8R8G8B8_PREMULTIPLIED;
#endif
	GdkTexture*
	    texture = gdk_memory_texture_new(width, height, format, bytes, stride);
	g_bytes_unref(bytes);
	gtk_image_set_from_paintable(GTK_IMAGE(source), GDK_PAINTABLE(texture));
	g_object_unref(texture);
}
static GtkWidget* file_widget(
    SignHome*                 self,
    const sign_history_entry* entry,
    bool                      grid
) {
	GFile*     file = g_file_new_for_path(entry->path);
	GtkWidget* box = gtk_box_new(
	    grid ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL,
	    grid ? 4 : 8
	);
	gtk_widget_set_margin_start(box, 4);
	gtk_widget_set_margin_end(box, 4);
	gtk_widget_set_margin_top(box, grid ? 4 : 0);
	gtk_widget_set_margin_bottom(box, grid ? 4 : 0);
	GtkWidget* item = grid ? box : gtk_list_box_row_new();
	g_object_set_data(G_OBJECT(item), "home-item", GINT_TO_POINTER(1));
	g_object_set_data_full(
	    G_OBJECT(item),
	    "home-path",
	    g_strdup(entry->path),
	    g_free
	);
	if (!grid) {
		gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(item), box);
		gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(item), TRUE);
		gtk_actionable_set_action_name(GTK_ACTIONABLE(item), "document.open");
	}
	sign_file_actions_attach(
	    item,
	    file,
	    true,
	    entry->favorite,
	    favorite_changed,
	    self
	);
	sign_file_allow_validation(item);
	sign_file_drag_attach(item, file);
	GtkDragSource* source = g_object_get_data(
	    G_OBJECT(item),
	    "crsign-file-drag"
	);
	if (source) {
		g_object_set_data(G_OBJECT(source), "home-item", item);
		g_signal_connect(
		    source,
		    "prepare",
		    G_CALLBACK(home_drag_prepare),
		    self
		);
	}
	GtkGesture* click = gtk_gesture_click_new();
	gtk_gesture_single_set_button(
	    GTK_GESTURE_SINGLE(click),
	    GDK_BUTTON_PRIMARY
	);
	g_signal_connect(click, "pressed", G_CALLBACK(item_pressed), self);
	g_signal_connect(click, "released", G_CALLBACK(item_released), self);
	gtk_widget_add_controller(item, GTK_EVENT_CONTROLLER(click));
	if (source) {
		gtk_gesture_group(click, GTK_GESTURE(source));
	}
	g_object_unref(file);
	char*      name = g_filename_display_basename(entry->path);
	GtkWidget* title = gtk_label_new(name);
	g_free(name);
	gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_MIDDLE);
	gtk_widget_set_tooltip_text(title, entry->path);
	if (grid) {
		GtkWidget* image = gtk_image_new_from_icon_name(
		    "text-x-generic-symbolic"
		);
		gtk_image_set_pixel_size(GTK_IMAGE(image), 128);
		gtk_widget_set_size_request(image, 128, 128);
		gtk_label_set_max_width_chars(GTK_LABEL(title), 18);
		gtk_label_set_wrap(GTK_LABEL(title), TRUE);
		gtk_label_set_wrap_mode(GTK_LABEL(title), PANGO_WRAP_WORD_CHAR);
		gtk_label_set_lines(GTK_LABEL(title), 2);
		gtk_label_set_justify(GTK_LABEL(title), GTK_JUSTIFY_CENTER);
		gtk_box_append(GTK_BOX(box), image);
		gtk_widget_set_size_request(box, 146, -1);
		gtk_widget_add_css_class(box, "home-file-tile");
		thumbnail_job* job = g_new0(thumbnail_job, 1);
		job->path = g_strdup(entry->path);
		GTask* task = g_task_new(
		    image,
		    self->thumbnails,
		    thumbnail_finished,
		    NULL
		);
		g_task_set_task_data(task, job, thumbnail_free);
		g_task_run_in_thread(task, thumbnail_worker);
		g_object_unref(task);
	} else {
		gtk_label_set_xalign(GTK_LABEL(title), 0);
		gtk_widget_set_hexpand(title, TRUE);
	}
	gtk_box_append(GTK_BOX(box), title);
	if (!grid) {
		char*      date = date_text(entry->signed_at);
		GtkWidget* stamp = gtk_label_new(date);
		g_free(date);
		gtk_widget_set_name(stamp, "document-signed-date");
		gtk_widget_add_css_class(stamp, "dim-label");
		gtk_widget_add_css_class(stamp, "caption");
		gtk_label_set_width_chars(GTK_LABEL(stamp), 20);
		GDateTime* local = g_date_time_new_from_unix_local(
		    entry->signed_at / G_USEC_PER_SEC
		);
		char* full = local ? g_date_time_format(local, "%c") : NULL;
		gtk_widget_set_tooltip_text(stamp, full);
		g_free(full);
		if (local) {
			g_date_time_unref(local);
		}
		gtk_box_append(GTK_BOX(box), stamp);
		gtk_box_append(
		    GTK_BOX(box),
		    sign_file_favorite_button(entry->favorite)
		);
	}
	return item;
}
static void grid_file_activated(
    GtkFlowBox*      flow,
    GtkFlowBoxChild* child,
    gpointer         data
) {
	(void)flow;
	(void)data;
	gtk_widget_activate_action(
	    gtk_flow_box_child_get_child(child),
	    "document.open",
	    NULL
	);
}
static void attach_section(GtkWidget* target, GtkWidget* list) {
	home_section* section = g_new0(home_section, 1);
	section->list = list;
	g_object_set_data_full(G_OBJECT(list), "home-section", section, g_free);
	gtk_box_append(GTK_BOX(target), list);
}
static GtkWidget* section_list(SignHome* self, GtkWidget* target) {
	clear(target);
	if (self->grid) {
		GtkWidget* flow = gtk_flow_box_new();
		gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
		gtk_flow_box_set_activate_on_single_click(GTK_FLOW_BOX(flow), FALSE);
		g_signal_connect(
		    flow,
		    "child-activated",
		    G_CALLBACK(grid_file_activated),
		    NULL
		);
		gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 1);
		gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 12);
		gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 4);
		gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 4);
		gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow), TRUE);
		attach_section(target, flow);
		return flow;
	}
	GtkWidget* columns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_margin_start(columns, 8);
	gtk_widget_set_margin_end(columns, 8);
	GtkWidget* label = gtk_label_new(_("Document"));
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_widget_set_hexpand(label, TRUE);
	gtk_box_append(GTK_BOX(columns), label);
	label = gtk_label_new(_("Signed"));
	gtk_label_set_width_chars(GTK_LABEL(label), 20);
	gtk_box_append(GTK_BOX(columns), label);
	label = gtk_label_new("");
	gtk_widget_set_size_request(label, 34, -1);
	gtk_box_append(GTK_BOX(columns), label);
	gtk_widget_add_css_class(columns, "dim-label");
	gtk_box_append(GTK_BOX(target), columns);
	GtkWidget* list = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
	gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(list), FALSE);
	gtk_widget_add_css_class(list, "home-file-list");
	attach_section(target, list);
	return list;
}
static void render_section(SignHome* self, bool favorites, size_t start) {
	GtkWidget* target = favorites ? self->favorites : self->recents;
	size_t     limit = favorites ? self->favorite_limit : self->recent_limit,
	       count = 0;
	GtkWidget* list = start
	                      ? g_object_get_data(G_OBJECT(target), "section-list")
	                      : section_list(self, target);
	g_object_set_data(G_OBJECT(target), "section-list", list);
	for (size_t n = 0; n < history_count(self->history); ++n) {
		const sign_history_entry* entry = history_get(self->history, n);
		if (favorites && !entry->favorite) {
			continue;
		}
		if (count++ >= limit || count <= start) {
			continue;
		}
		GtkWidget* widget = file_widget(self, entry, self->grid);
		if (self->grid) {
			gtk_flow_box_insert(GTK_FLOW_BOX(list), widget, -1);
		} else {
			gtk_list_box_append(GTK_LIST_BOX(list), widget);
		}
	}
	gtk_widget_set_visible(target, count > 0);
	gtk_widget_set_visible(
	    favorites ? self->favorite_heading : self->recent_heading,
	    count > 0
	);
	gtk_widget_set_visible(
	    favorites ? self->favorite_more : self->recent_more,
	    count > limit
	);
}
static void render_home(SignHome* self) {
	selection_clear(self);
	self->selection_anchor = NULL;
	g_cancellable_cancel(self->thumbnails);
	g_object_unref(self->thumbnails);
	self->thumbnails = g_cancellable_new();
	render_section(self, true, 0);
	render_section(self, false, 0);
}
static gboolean refresh(gpointer data) {
	SignHome* self = data;
	self->timer = 0;
	sign_home_reload(self);
	return G_SOURCE_REMOVE;
}
static void monitor_changed(
    GFileMonitor*     monitor,
    GFile*            file,
    GFile*            other,
    GFileMonitorEvent event,
    gpointer          data
) {
	(void)monitor;
	(void)event;
	SignHome* self = data;
	char*     name = file ? g_file_get_basename(file) : NULL;
	char*     renamed = other ? g_file_get_basename(other) : NULL;
	bool      relevant = !g_strcmp0(name, "history.json") ||
	                !g_strcmp0(renamed, "history.json");
	g_free(name);
	g_free(renamed);
	if (relevant && !self->timer) {
		self->timer = g_timeout_add(180, refresh, self);
	}
}
bool sign_home_has_documents(SignHome* self) {
	return history_count(self->history) > 0;
}
void sign_home_reload(SignHome* self) {
	GError*       error = NULL;
	sign_history* history = history_load(&error);
	gtk_widget_set_visible(self->notice, !history);
	if (!history) {
		g_clear_error(&error);
		return;
	}
	bool same = self->history &&
	            history_count(history) == history_count(self->history);
	for (size_t n = 0; same && n < history_count(history); ++n) {
		const sign_history_entry* a = history_get(history, n);
		const sign_history_entry* b = history_get(self->history, n);
		same = a->signed_at == b->signed_at && a->favorite == b->favorite &&
		       !strcmp(a->path, b->path);
	}
	if (same) {
		history_free(history);
		return;
	}
	bool previous = sign_home_has_documents(self);
	history_free(self->history);
	self->history = history;
	render_home(self);
	search_update(self);
	if (previous != sign_home_has_documents(self)) {
		g_signal_emit(self, history_changed, 0);
	}
	if (!self->monitor) {
		char*  path = history_path();
		GFile* file = g_file_new_for_path(path);
		g_free(path);
		GFile* directory = g_file_get_parent(file);
		g_object_unref(file);
		self->monitor = g_file_monitor_directory(
		    directory,
		    G_FILE_MONITOR_NONE,
		    NULL,
		    NULL
		);
		g_object_unref(directory);
		if (self->monitor) {
			g_signal_connect(
			    self->monitor,
			    "changed",
			    G_CALLBACK(monitor_changed),
			    self
			);
		}
	}
}
bool sign_home_is_grid(SignHome* self) {
	return self->grid;
}
void sign_home_set_grid(SignHome* self, bool grid) {
	if (self->grid == grid) {
		return;
	}
	self->grid = grid;
	render_home(self);
	GError* error = NULL;
	if (!ui_preferences_set_grid(grid, &error)) {
		gui_error_dialog(
		    parent_window(self),
		    _("Could not save display preference"),
		    _("The display preference could not be saved. Check the "
		      "application data file and its permissions.")
		);
		g_clear_error(&error);
	}
}
static void more(GtkButton* button, gpointer data) {
	SignHome* self = data;
	bool      favorites = GTK_WIDGET(button) == self->favorite_more;
	size_t    start = favorites ? self->favorite_limit : self->recent_limit;
	if (favorites) {
		self->favorite_limit += PAGE_SIZE;
	} else {
		self->recent_limit += PAGE_SIZE;
	}
	render_section(self, favorites, start);
}
static char* normalized(const char* text) {
	char* folded = g_utf8_casefold(text, -1);
	char* expanded = g_utf8_normalize(folded, -1, G_NORMALIZE_ALL);
	g_free(folded);
	GString* result = g_string_new(NULL);
	for (const char* p = expanded; p && *p; p = g_utf8_next_char(p)) {
		gunichar c = g_utf8_get_char(p);
		if (!g_unichar_ismark(c)) {
			g_string_append_unichar(result, c);
		}
	}
	g_free(expanded);
	return g_string_free(result, FALSE);
}
static int match_score(const char* name, const char* query) {
	if (!*query) {
		return 0;
	}
	const char* p = name;
	int         score = 0, position = 0;
	for (const char* q = query; *q; q = g_utf8_next_char(q)) {
		gunichar target = g_utf8_get_char(q);
		bool     found = false;
		for (; *p; p = g_utf8_next_char(p), ++position) {
			if (g_utf8_get_char(p) == target) {
				score += position;
				p = g_utf8_next_char(p);
				++position;
				found = true;
				break;
			}
		}
		if (!found) {
			return -1;
		}
	}
	return score;
}
typedef struct {
	const sign_history_entry* entry;
	int                       score;
} search_match;
static gint compare_match(gconstpointer a, gconstpointer b) {
	const search_match* x = a;
	const search_match* y = b;
	if (x->score != y->score) {
		return x->score < y->score ? -1 : 1;
	}
	return x->entry->signed_at > y->entry->signed_at
	           ? -1
	           : x->entry->signed_at < y->entry->signed_at;
}
static void search_result_activated(GtkListBoxRow* row, gpointer data);
static void search_update(SignHome* self) {
	if (!self->search_rows) {
		return;
	}
	clear(self->search_rows);
	char* query = normalized(
	    gtk_editable_get_text(GTK_EDITABLE(self->search_entry))
	);
	GArray* matches = g_array_new(FALSE, FALSE, sizeof(search_match));
	for (size_t n = 0; n < history_count(self->history); ++n) {
		const sign_history_entry* entry = history_get(self->history, n);
		char* basename = g_filename_display_basename(entry->path);
		char* name = normalized(basename);
		g_free(basename);
		int score = match_score(name, query);
		g_free(name);
		if (score >= 0) {
			search_match match = {entry, score};
			g_array_append_val(matches, match);
		}
	}
	g_free(query);
	g_array_sort(matches, compare_match);
	for (guint n = 0; n < matches->len && n < self->search_limit; ++n) {
		const sign_history_entry*
		           entry = g_array_index(matches, search_match, n).entry;
		GtkWidget* row = adw_action_row_new();
		adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
		char* name = g_filename_display_basename(entry->path);
		adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), name);
		g_free(name);
		adw_action_row_set_subtitle(ADW_ACTION_ROW(row), entry->path);
		adw_action_row_set_subtitle_lines(ADW_ACTION_ROW(row), 1);
		GFile* file = g_file_new_for_path(entry->path);
		sign_file_actions_attach(row, file, false, false, NULL, NULL);
		sign_file_allow_validation(row);
		sign_file_drag_attach(row, file);
		g_object_unref(file);
		gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), TRUE);
		gtk_actionable_set_action_name(GTK_ACTIONABLE(row), "document.open");
		g_signal_connect_object(
		    row,
		    "activate",
		    G_CALLBACK(search_result_activated),
		    self,
		    G_CONNECT_AFTER
		);
		gtk_list_box_append(GTK_LIST_BOX(self->search_rows), row);
	}
	if (!matches->len) {
		GtkWidget* label = gtk_label_new(_("No matching documents"));
		gtk_widget_add_css_class(label, "dim-label");
		GtkWidget* row = gtk_list_box_row_new();
		gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
		gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
		gtk_list_box_append(GTK_LIST_BOX(self->search_rows), row);
	}
	gtk_widget_set_visible(
	    self->search_more,
	    matches->len > self->search_limit
	);
	g_array_unref(matches);
}
static void search_result_activated(GtkListBoxRow* row, gpointer data) {
	(void)row;
	SignHome* self = data;
	gtk_menu_button_popdown(GTK_MENU_BUTTON(self->search_button));
}
static void search_enter(GtkSearchEntry* entry, gpointer data) {
	(void)entry;
	SignHome* self = data;
	search_update(self);
	GtkListBoxRow* row = gtk_list_box_get_row_at_index(
	    GTK_LIST_BOX(self->search_rows),
	    0
	);
	if (row && gtk_list_box_row_get_activatable(row)) {
		gtk_widget_activate(GTK_WIDGET(row));
	}
}
static void query_changed(GtkSearchEntry* entry, gpointer data) {
	(void)entry;
	SignHome* self = data;
	self->search_limit = PAGE_SIZE;
	search_update(self);
}
static void search_more(GtkButton* button, gpointer data) {
	(void)button;
	SignHome* self = data;
	self->search_limit += PAGE_SIZE;
	search_update(self);
}
static void search_opened(
    GObject*    object,
    GParamSpec* property,
    gpointer    data
) {
	(void)property;
	if (gtk_widget_get_visible(GTK_WIDGET(object))) {
		SignHome* self = data;
		sign_home_reload(self);
		gtk_widget_grab_focus(self->search_entry);
	}
}
GtkWidget* sign_home_search_button(SignHome* self) {
	return self->search_button;
}
static void dispose(GObject* object) {
	SignHome* self = SIGN_HOME(object);
	self->closing = true;
	if (self->timer) {
		g_source_remove(self->timer);
		self->timer = 0;
	}
	if (self->monitor) {
		g_file_monitor_cancel(self->monitor);
		g_object_unref(self->monitor);
		self->monitor = NULL;
	}
	g_cancellable_cancel(self->thumbnails);
	if (self->search_button) {
		g_object_unref(self->search_button);
		self->search_button = NULL;
	}
	G_OBJECT_CLASS(sign_home_parent_class)->dispose(object);
}
static void finalize(GObject* object) {
	SignHome* self = SIGN_HOME(object);
	history_free(self->history);
	g_object_unref(self->thumbnails);
	g_hash_table_unref(self->selected);
	G_OBJECT_CLASS(sign_home_parent_class)->finalize(object);
}
static void sign_home_root(GtkWidget* widget) {
	GTK_WIDGET_CLASS(sign_home_parent_class)->root(widget);
	SignHome* self = SIGN_HOME(widget);
	self->outside_click = gtk_gesture_click_new();
	gtk_gesture_single_set_button(
	    GTK_GESTURE_SINGLE(self->outside_click),
	    GDK_BUTTON_PRIMARY
	);
	gtk_event_controller_set_propagation_phase(
	    GTK_EVENT_CONTROLLER(self->outside_click),
	    GTK_PHASE_CAPTURE
	);
	g_signal_connect(
	    self->outside_click,
	    "pressed",
	    G_CALLBACK(outside_pressed),
	    self
	);
	gtk_widget_add_controller(
	    GTK_WIDGET(gtk_widget_get_root(widget)),
	    GTK_EVENT_CONTROLLER(self->outside_click)
	);
}
static void sign_home_unroot(GtkWidget* widget) {
	SignHome* self = SIGN_HOME(widget);
	if (self->outside_click) {
		gtk_widget_remove_controller(
		    GTK_WIDGET(gtk_widget_get_root(widget)),
		    GTK_EVENT_CONTROLLER(self->outside_click)
		);
		self->outside_click = NULL;
	}
	GTK_WIDGET_CLASS(sign_home_parent_class)->unroot(widget);
}
static void sign_home_class_init(SignHomeClass* klass) {
	GTK_WIDGET_CLASS(klass)->root = sign_home_root;
	GTK_WIDGET_CLASS(klass)->unroot = sign_home_unroot;
	history_changed = g_signal_new(
	    "history-changed",
	    G_TYPE_FROM_CLASS(klass),
	    G_SIGNAL_RUN_LAST,
	    0,
	    NULL,
	    NULL,
	    NULL,
	    G_TYPE_NONE,
	    0
	);
	G_OBJECT_CLASS(klass)->dispose = dispose;
	G_OBJECT_CLASS(klass)->finalize = finalize;
}
static void sign_home_init(SignHome* self) {
	self->grid = true;
	self->selected = g_hash_table_new(g_direct_hash, g_direct_equal);
	ui_preferences_grid(&self->grid, NULL);
	self->favorite_limit = self->recent_limit = self->search_limit = PAGE_SIZE;
	self->thumbnails = g_cancellable_new();
	gtk_orientable_set_orientation(
	    GTK_ORIENTABLE(self),
	    GTK_ORIENTATION_VERTICAL
	);
	self->notice = gtk_label_new(
	    _("The document history could not be read. It may be invalid or exceed "
	      "its size limit.")
	);
	gtk_label_set_wrap(GTK_LABEL(self->notice), TRUE);
	gtk_widget_add_css_class(self->notice, "error");
	gtk_widget_set_visible(self->notice, FALSE);
	gtk_box_append(GTK_BOX(self), self->notice);
	GtkWidget* scroll = gtk_scrolled_window_new();
	gtk_widget_set_vexpand(scroll, TRUE);
	gtk_scrolled_window_set_policy(
	    GTK_SCROLLED_WINDOW(scroll),
	    GTK_POLICY_NEVER,
	    GTK_POLICY_AUTOMATIC
	);
	gtk_box_append(GTK_BOX(self), scroll);
	GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_start(content, 18);
	gtk_widget_set_margin_end(content, 18);
	gtk_widget_set_margin_top(content, 18);
	gtk_widget_set_margin_bottom(content, 18);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), content);
	GtkWidget* heading = gtk_label_new(_("Favorites"));
	self->favorite_heading = heading;
	gtk_widget_set_halign(heading, GTK_ALIGN_START);
	gtk_widget_add_css_class(heading, "title-2");
	gtk_box_append(GTK_BOX(content), heading);
	self->favorites = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_name(self->favorites, "home-favorites");
	gtk_box_append(GTK_BOX(content), self->favorites);
	self->favorite_more = gtk_button_new_with_label(_("Show more"));
	gtk_widget_set_halign(self->favorite_more, GTK_ALIGN_CENTER);
	gtk_widget_set_name(self->favorite_more, "home-favorites-more");
	g_signal_connect(self->favorite_more, "clicked", G_CALLBACK(more), self);
	gtk_box_append(GTK_BOX(content), self->favorite_more);
	heading = gtk_label_new(_("Recent"));
	self->recent_heading = heading;
	gtk_widget_set_halign(heading, GTK_ALIGN_START);
	gtk_widget_add_css_class(heading, "title-2");
	gtk_widget_set_margin_top(heading, 12);
	gtk_box_append(GTK_BOX(content), heading);
	self->recents = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_name(self->recents, "home-recents");
	gtk_box_append(GTK_BOX(content), self->recents);
	self->recent_more = gtk_button_new_with_label(_("Show more"));
	gtk_widget_set_halign(self->recent_more, GTK_ALIGN_CENTER);
	gtk_widget_set_name(self->recent_more, "home-recents-more");
	g_signal_connect(self->recent_more, "clicked", G_CALLBACK(more), self);
	gtk_box_append(GTK_BOX(content), self->recent_more);
	self->search_button = g_object_ref_sink(gtk_menu_button_new());
	gtk_menu_button_set_icon_name(
	    GTK_MENU_BUTTON(self->search_button),
	    "system-search-symbolic"
	);
	gtk_widget_add_css_class(self->search_button, "flat");
	gtk_widget_set_name(self->search_button, "history-search");
	gtk_widget_set_tooltip_text(
	    self->search_button,
	    _("Search signed documents")
	);
	GtkWidget* popover = gtk_popover_new();
	gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
	gtk_widget_set_name(popover, "history-search-popup");
	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_start(box, 12);
	gtk_widget_set_margin_end(box, 12);
	gtk_widget_set_margin_top(box, 12);
	gtk_widget_set_margin_bottom(box, 12);
	gtk_widget_set_size_request(box, 320, -1);
	self->search_entry = gtk_search_entry_new();
	gtk_widget_set_name(self->search_entry, "history-query");
	gtk_widget_set_tooltip_text(
	    self->search_entry,
	    _("Search by document name")
	);
	gtk_box_append(GTK_BOX(box), self->search_entry);
	GtkWidget* results = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(
	    GTK_SCROLLED_WINDOW(results),
	    GTK_POLICY_NEVER,
	    GTK_POLICY_AUTOMATIC
	);
	gtk_scrolled_window_set_min_content_height(
	    GTK_SCROLLED_WINDOW(results),
	    64
	);
	gtk_scrolled_window_set_max_content_height(
	    GTK_SCROLLED_WINDOW(results),
	    360
	);
	gtk_scrolled_window_set_propagate_natural_height(
	    GTK_SCROLLED_WINDOW(results),
	    TRUE
	);
	self->search_rows = gtk_list_box_new();
	gtk_list_box_set_selection_mode(
	    GTK_LIST_BOX(self->search_rows),
	    GTK_SELECTION_NONE
	);
	gtk_widget_set_name(self->search_rows, "history-search-results");
	gtk_widget_add_css_class(self->search_rows, "home-file-list");
	g_signal_connect_object(
	    self->search_entry,
	    "activate",
	    G_CALLBACK(search_enter),
	    self,
	    0
	);
	gtk_scrolled_window_set_child(
	    GTK_SCROLLED_WINDOW(results),
	    self->search_rows
	);
	gtk_box_append(GTK_BOX(box), results);
	self->search_more = gtk_button_new_with_label(_("Show more"));
	g_signal_connect(
	    self->search_more,
	    "clicked",
	    G_CALLBACK(search_more),
	    self
	);
	gtk_box_append(GTK_BOX(box), self->search_more);
	gtk_popover_set_child(GTK_POPOVER(popover), box);
	gtk_menu_button_set_popover(GTK_MENU_BUTTON(self->search_button), popover);
	g_signal_connect_object(
	    self->search_entry,
	    "search-changed",
	    G_CALLBACK(query_changed),
	    self,
	    0
	);
	g_signal_connect_object(
	    popover,
	    "notify::visible",
	    G_CALLBACK(search_opened),
	    self,
	    0
	);
	sign_home_reload(self);
}
GtkWidget* sign_home_new(void) {
	return g_object_new(SIGN_TYPE_HOME, NULL);
}
