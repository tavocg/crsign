/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "helpers.h"
#include <glib/gstdio.h>
#include <sign/gui/home.h>
#include <sign/gui/i18n.h>
#include <sign/history.h>
static guint count(GtkWidget* parent) {
	guint n = 0;
	for (GtkWidget* child = gtk_widget_get_first_child(parent); child;
	     child = gtk_widget_get_next_sibling(child)) {
		++n;
	}
	return n;
}
static GtkWidget* flow(GtkWidget* self, const char* name) {
	GtkWidget* result = find_widget(named(self, name), GTK_TYPE_FLOW_BOX);
	g_assert_nonnull(result);
	return result;
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
static GtkWidget* context_open(GtkWidget* window, GtkWidget* item) {
	char* tool = g_find_program_in_path("xdotool");
	if (!tool) {
		return NULL;
	}
	gtk_widget_grab_focus(gtk_widget_get_parent(item));
	settle();
	graphene_rect_t bounds;
	g_assert_true(gtk_widget_compute_bounds(item, window, &bounds));
	char* id = NULL;
	int   status = 0;
	char* args[] = {tool, "search", "--name", "^CRSign Home Test$", NULL};
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
	char* command = g_strdup_printf(
	    "%s mousemove --window %s %d %d click 3",
	    tool,
	    id,
	    (int)bounds.origin.x + 24,
	    (int)bounds.origin.y + 24
	);
	g_assert_true(
	    g_spawn_command_line_sync(command, NULL, NULL, &status, NULL)
	);
	g_free(command);
	g_free(id);
	g_free(tool);
	settle();
	GtkWidget* menu = find_widget(item, GTK_TYPE_POPOVER_MENU);
	g_assert_nonnull(menu);
	g_assert_true(gtk_widget_get_mapped(menu));
	return menu;
}
typedef struct {
	GFile*       expected;
	unsigned int calls;
} file_manager;
static void show_items(
    GDBusConnection*       connection,
    const char*            sender,
    const char*            object,
    const char*            interface,
    const char*            method,
    GVariant*              parameters,
    GDBusMethodInvocation* invocation,
    gpointer               data
) {
	(void)connection;
	(void)sender;
	(void)object;
	(void)interface;
	file_manager* manager = data;
	g_assert_cmpstr(method, ==, "ShowItems");
	GVariant* uris = g_variant_get_child_value(parameters, 0);
	g_assert_cmpuint(g_variant_n_children(uris), ==, 1);
	GVariant* uri = g_variant_get_child_value(uris, 0);
	GFile*    file = g_file_new_for_uri(g_variant_get_string(uri, NULL));
	g_assert_true(g_file_equal(file, manager->expected));
	g_object_unref(file);
	g_variant_unref(uri);
	g_variant_unref(uris);
	++manager->calls;
	g_dbus_method_invocation_return_value(invocation, NULL);
}
int main(int argc, char** argv) {
	g_assert_cmpint(argc, ==, 3);
	gui_i18n_init();
	adw_init();
	GtkCssProvider* style = gtk_css_provider_new();
	gtk_css_provider_load_from_resource(style, "/org/crsign/Sign/style.css");
	gtk_style_context_add_provider_for_display(
	    gdk_display_get_default(),
	    GTK_STYLE_PROVIDER(style),
	    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
	);
	g_object_unref(style);
	g_mkdir_with_parents(argv[2], 0700);
	for (unsigned int n = 0; n < 57; ++n) {
		char* path = g_strdup_printf(
		    "%s/Documento firmado número %02u.pdf",
		    argv[1],
		    n
		);
		g_assert_true(history_record(path, NULL));
		if (n < 35) {
			g_assert_true(history_favorite(path, true, NULL));
		}
		g_free(path);
	}
	AdwApplication* app = adw_application_new(
	    "org.crsign.Sign.HomeTest",
	    G_APPLICATION_NON_UNIQUE
	);
	g_assert_true(g_application_register(G_APPLICATION(app), NULL, NULL));
	GtkWidget* window = adw_application_window_new(GTK_APPLICATION(app));
	gtk_window_set_default_size(GTK_WINDOW(window), 920, 760);
	gtk_window_set_title(GTK_WINDOW(window), "CRSign Home Test");
	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	SignHome*  self = SIGN_HOME(sign_home_new());
	GtkWidget* header = adw_header_bar_new();
	adw_header_bar_pack_start(
	    ADW_HEADER_BAR(header),
	    sign_home_search_button(self)
	);
	gtk_box_append(GTK_BOX(box), header);
	gtk_box_append(GTK_BOX(box), GTK_WIDGET(self));
	adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), box);
	gtk_window_present(GTK_WINDOW(window));
	settle();
	g_assert_cmpuint(count(flow(GTK_WIDGET(self), "home-favorites")), ==, 25);
	g_assert_cmpuint(count(flow(GTK_WIDGET(self), "home-recents")), ==, 25);
	GtkWidget* first = gtk_flow_box_child_get_child(
	    gtk_flow_box_get_child_at_index(
	        GTK_FLOW_BOX(flow(GTK_WIDGET(self), "home-recents")),
	        0
	    )
	);
	GtkImage* image = GTK_IMAGE(find_widget(first, GTK_TYPE_IMAGE));
	gint64    deadline = g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND;
	while (!GDK_IS_TEXTURE(gtk_image_get_paintable(image)) &&
	       g_get_monotonic_time() < deadline) {
		while (g_main_context_iteration(NULL, FALSE)) {
		}
		g_usleep(1000);
	}
	g_assert_true(GDK_IS_TEXTURE(gtk_image_get_paintable(image)));
	g_assert_null(named(first, "document-location"));
	g_assert_null(named(first, "document-favorite"));
	g_assert_null(named(first, "document-signed-date"));
	g_assert_false(gtk_widget_has_css_class(first, "card"));
	/* Intercept the native reveal request on the private test bus. */
	GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
	g_assert_nonnull(bus);
	GVariant* owned = g_dbus_connection_call_sync(
	    bus,
	    "org.freedesktop.DBus",
	    "/org/freedesktop/DBus",
	    "org.freedesktop.DBus",
	    "RequestName",
	    g_variant_new("(su)", "org.freedesktop.FileManager1", 0u),
	    G_VARIANT_TYPE("(u)"),
	    G_DBUS_CALL_FLAGS_NONE,
	    5000,
	    NULL,
	    NULL
	);
	g_assert_nonnull(owned);
	g_variant_unref(owned);
	GDBusNodeInfo* info = g_dbus_node_info_new_for_xml(
	    "<node><interface name='org.freedesktop.FileManager1'><method "
	    "name='ShowItems'><arg type='as' direction='in'/><arg type='s' "
	    "direction='in'/></method></interface></node>",
	    NULL
	);
	char* expected = g_strdup_printf(
	    "%s/Documento firmado número 56.pdf",
	    argv[1]
	);
	file_manager manager = {g_file_new_for_path(expected), 0};
	g_free(expected);
	const GDBusInterfaceVTable vtable = {.method_call = show_items};
	guint                      registration = g_dbus_connection_register_object(
        bus,
        "/org/freedesktop/FileManager1",
        info->interfaces[0],
        &vtable,
        &manager,
        NULL,
        NULL
    );
	g_assert_cmpuint(registration, >, 0);
	GtkWidget* location_menu = context_open(window, first);
	g_assert_nonnull(location_menu);
	GtkWidget* location_item = find_widget(
	    location_menu,
	    g_type_from_name("GtkModelButton")
	);
	g_assert_nonnull(location_item);
	g_assert_true(gtk_widget_activate(location_item));
	deadline = g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND;
	while (!manager.calls && g_get_monotonic_time() < deadline) {
		while (g_main_context_iteration(NULL, FALSE)) {
		}
		g_usleep(1000);
	}
	g_assert_cmpuint(manager.calls, ==, 1);
	g_dbus_connection_unregister_object(bus, registration);
	g_dbus_node_info_unref(info);
	g_object_unref(manager.expected);
	g_object_unref(bus);

	snapshot(window, argv[2], "home-thumbnails.png");
	g_signal_emit_by_name(
	    named(GTK_WIDGET(self), "home-favorites-more"),
	    "clicked"
	);
	g_assert_cmpuint(count(flow(GTK_WIDGET(self), "home-favorites")), ==, 35);
	g_assert_false(
	    gtk_widget_get_visible(named(GTK_WIDGET(self), "home-favorites-more"))
	);
	g_signal_emit_by_name(
	    named(GTK_WIDGET(self), "home-recents-more"),
	    "clicked"
	);
	g_assert_cmpuint(count(flow(GTK_WIDGET(self), "home-recents")), ==, 50);
	g_signal_emit_by_name(
	    named(GTK_WIDGET(self), "home-recents-more"),
	    "clicked"
	);
	g_assert_cmpuint(count(flow(GTK_WIDGET(self), "home-recents")), ==, 57);
	g_assert_false(
	    gtk_widget_get_visible(named(GTK_WIDGET(self), "home-recents-more"))
	);
	/* Only Recent exposes the favorite action; it persists across reloads. */
	GtkWidget* favorite = gtk_flow_box_child_get_child(
	    gtk_flow_box_get_child_at_index(
	        GTK_FLOW_BOX(flow(GTK_WIDGET(self), "home-favorites")),
	        0
	    )
	);
	g_assert_null(named(favorite, "document-favorite"));
	first = gtk_flow_box_child_get_child(gtk_flow_box_get_child_at_index(
	    GTK_FLOW_BOX(flow(GTK_WIDGET(self), "home-recents")),
	    0
	));
	g_assert_true(gtk_widget_activate_action(first, "document.favorite", NULL));
	sign_history* clicked = history_load(NULL);
	g_assert_true(history_get(clicked, 0)->favorite);
	history_free(clicked);
	first = gtk_flow_box_child_get_child(gtk_flow_box_get_child_at_index(
	    GTK_FLOW_BOX(flow(GTK_WIDGET(self), "home-recents")),
	    0
	));
	g_assert_true(gtk_widget_activate_action(first, "document.favorite", NULL));
	first = gtk_flow_box_child_get_child(gtk_flow_box_get_child_at_index(
	    GTK_FLOW_BOX(flow(GTK_WIDGET(self), "home-recents")),
	    0
	));
	settle();
	first = gtk_flow_box_child_get_child(gtk_flow_box_get_child_at_index(
	    GTK_FLOW_BOX(flow(GTK_WIDGET(self), "home-recents")),
	    0
	));
	GtkWidget* menu = context_open(window, first);
	if (menu) {
		g_assert_cmpint(
		    g_menu_model_get_n_items(
		        gtk_popover_menu_get_menu_model(GTK_POPOVER_MENU(menu))
		    ),
		    ==,
		    2
		);
		snapshot(menu, argv[2], "home-document-menu.png");
		GtkWidget* location_item = find_widget(
		    menu,
		    g_type_from_name("GtkModelButton")
		);
		GtkWidget* favorite_item = gtk_widget_get_next_sibling(location_item);
		g_assert_nonnull(favorite_item);
		g_assert_true(gtk_widget_activate(favorite_item));
		settle();
	} else {
		g_assert_true(
		    gtk_widget_activate_action(first, "document.favorite", NULL)
		);
	}
	sign_history* history = history_load(NULL);
	g_assert_true(history_get(history, 0)->favorite);
	history_free(history);
	sign_home_reload(self);
	sign_home_set_grid(self, false);
	settle();
	g_assert_nonnull(
	    find_widget(named(GTK_WIDGET(self), "home-recents"), GTK_TYPE_LIST_BOX)
	);
	g_assert_true(contains(GTK_WIDGET(self), "Documento"));
	g_assert_true(contains(GTK_WIDGET(self), "Firmado"));
	GtkWidget* list = find_widget(
	    named(GTK_WIDGET(self), "home-recents"),
	    GTK_TYPE_LIST_BOX
	);
	GtkWidget* row = GTK_WIDGET(
	    gtk_list_box_get_row_at_index(GTK_LIST_BOX(list), 0)
	);
	g_assert_nonnull(named(row, "document-location"));
	g_assert_nonnull(named(row, "document-favorite"));
	GtkWidget* stamp = named(row, "document-signed-date");
	g_assert_nonnull(stamp);
	g_assert_true(
	    g_str_has_prefix(gtk_label_get_text(GTK_LABEL(stamp)), "Hoy · ")
	);
	g_assert_nonnull(gtk_widget_get_tooltip_text(stamp));
	snapshot(window, argv[2], "home-list.png");
	gtk_menu_button_popup(GTK_MENU_BUTTON(sign_home_search_button(self)));
	settle();
	GtkWidget* popover = GTK_WIDGET(gtk_menu_button_get_popover(
	    GTK_MENU_BUTTON(sign_home_search_button(self))
	));
	g_assert_true(gtk_widget_get_mapped(popover));
	gtk_editable_set_text(
	    GTK_EDITABLE(named(popover, "history-query")),
	    "DCfmnm56"
	);
	settle();
	GtkWidget* results = named(popover, "history-search-results");
	g_assert_true(contains(results, "Documento firmado número 56.pdf"));
	g_assert_cmpuint(count(results), ==, 1);
	g_assert_nonnull(named(results, "document-location"));
	g_assert_null(named(results, "document-favorite"));
	snapshot(window, argv[2], "home-search.png");
	gtk_menu_button_popdown(GTK_MENU_BUTTON(sign_home_search_button(self)));
	sign_home_set_grid(self, true);
	gtk_window_set_default_size(GTK_WINDOW(window), 420, 760);
	settle();
	g_assert_cmpint(gtk_widget_get_width(window), <=, 420);
	snapshot(window, argv[2], "home-narrow.png");
	gtk_window_destroy(GTK_WINDOW(window));
	settle();
	g_object_unref(app);
	return 0;
}
