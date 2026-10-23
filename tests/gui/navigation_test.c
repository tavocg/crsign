/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <glib/gstdio.h>
#include <sign/config.h>
#include <sign/gui/i18n.h>
#include <sign/gui/settings.h>
#include <sign/gui/window.h>
#include <string.h>

static void settle(void) {
	gint64 deadline = g_get_monotonic_time() + 250 * G_TIME_SPAN_MILLISECOND;
	do {
		while (g_main_context_iteration(NULL, FALSE)) {
		}
		g_usleep(1000);
	} while (g_get_monotonic_time() < deadline);
}

static GtkWidget* find_widget(GtkWidget* root, GType type) {
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
static void settings_test(GtkWidget* window, const char* directory) {
	SignSettings* settings = SIGN_SETTINGS(
	    find_widget(window, SIGN_TYPE_SETTINGS)
	);
	char* path = config_path();
	g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
	GtkWidget* token = named(GTK_WIDGET(settings), "token");
	gtk_editable_set_text(GTK_EDITABLE(token), "pkcs11:token=Global");
	/* The normal debounce creates the file after a valid edit. */
	for (int i = 0; i < 3; ++i) {
		settle();
	}
	g_assert_true(g_file_test(path, G_FILE_TEST_IS_REGULAR));
	char      detail[512];
	sign_args args = {0};
	g_assert_true(config_load(&args, NULL, detail, sizeof(detail)));
	g_assert_cmpstr(args.options.uri, ==, "pkcs11:token=Global");
	arg_free(&args);
	g_signal_emit_by_name(
	    named(GTK_WIDGET(settings), "add-profile"),
	    "clicked"
	);
	gtk_editable_set_text(
	    GTK_EDITABLE(named(GTK_WIDGET(settings), "label")),
	    "bccr"
	);
	gtk_editable_set_text(
	    GTK_EDITABLE(named(GTK_WIDGET(settings), "name")),
	    "Banco ñ"
	);
	gtk_editable_set_text(GTK_EDITABLE(token), "pkcs11:token=Profile");
	g_assert_true(sign_settings_flush(settings));
	g_assert_true(config_load(&args, "bccr", detail, sizeof(detail)));
	g_assert_cmpstr(args.options.uri, ==, "pkcs11:token=Profile");
	arg_free(&args);
	gtk_editable_set_text(GTK_EDITABLE(token), "");
	g_assert_true(sign_settings_flush(settings));
	g_assert_true(config_load(&args, "bccr", detail, sizeof(detail)));
	g_assert_cmpstr(args.options.uri, ==, "pkcs11:token=Global");
	arg_free(&args);
	gtk_editable_set_text(
	    GTK_EDITABLE(named(GTK_WIDGET(settings), "label")),
	    ""
	);
	g_assert_false(sign_settings_flush(settings));
	g_assert_true(config_load(&args, "bccr", detail, sizeof(detail)));
	arg_free(&args);
	gtk_editable_set_text(
	    GTK_EDITABLE(named(GTK_WIDGET(settings), "label")),
	    "bccr"
	);
	g_assert_true(sign_settings_flush(settings));
	/* Reopening the view reads global values and the full profile list. */
	GtkWidget* reopened = sign_settings_new();
	g_object_ref_sink(reopened);
	g_assert_cmpstr(
	    gtk_editable_get_text(GTK_EDITABLE(named(reopened, "token"))),
	    ==,
	    "pkcs11:token=Global"
	);
	GtkDropDown* scopes = GTK_DROP_DOWN(named(reopened, "settings-scope"));
	g_assert_cmpuint(
	    g_list_model_get_n_items(gtk_drop_down_get_model(scopes)),
	    ==,
	    2
	);
	gtk_drop_down_set_selected(scopes, 1);
	g_assert_cmpstr(
	    gtk_editable_get_text(GTK_EDITABLE(named(reopened, "label"))),
	    ==,
	    "bccr"
	);
	g_object_unref(reopened);
	(void)directory;
	g_free(path);
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

int main(int argc, char** argv) {
	const char* directory = argc > 1 ? argv[1] : NULL;
	if (directory) {
		g_assert_cmpint(g_mkdir_with_parents(directory, 0700), ==, 0);
	}
	char* settings_directory = g_dir_make_tmp("crsign-navigation-XXXXXX", NULL);
	g_setenv("XDG_CONFIG_HOME", settings_directory, TRUE);
	gui_i18n_init();
	adw_init();
	adw_style_manager_set_color_scheme(
	    adw_style_manager_get_default(),
	    ADW_COLOR_SCHEME_FORCE_DARK
	);
	AdwApplication* app = adw_application_new(
	    "org.crsign.Sign.NavigationTest",
	    G_APPLICATION_NON_UNIQUE
	);
	GError* error = NULL;
	g_assert_true(g_application_register(G_APPLICATION(app), NULL, &error));
	g_assert_no_error(error);
	GtkWidget* window = sign_window_new(GTK_APPLICATION(app));
	gtk_window_present(GTK_WINDOW(window));
	settle();
	AdwOverlaySplitView* split = ADW_OVERLAY_SPLIT_VIEW(
	    find_widget(window, ADW_TYPE_OVERLAY_SPLIT_VIEW)
	);
	GtkStack*        stack = GTK_STACK(find_widget(window, GTK_TYPE_STACK));
	GtkListBox*      navigation = GTK_LIST_BOX(named(window, "navigation"));
	GtkToggleButton* toggle = GTK_TOGGLE_BUTTON(named(window, "menu-toggle"));
	g_assert_nonnull(split);
	g_assert_nonnull(stack);
	g_assert_nonnull(navigation);
	g_assert_nonnull(toggle);
	g_assert_cmpstr(
	    gtk_button_get_icon_name(GTK_BUTTON(toggle)),
	    ==,
	    "sidebar-show-symbolic"
	);
	g_assert_false(adw_overlay_split_view_get_collapsed(split));
	g_assert_true(adw_overlay_split_view_get_show_sidebar(split));
	g_assert_false(gtk_widget_get_visible(GTK_WIDGET(toggle)));
	const char* ids[] = {"home", "sign", "verify", "settings", "about"};
	const char* titles[] =
	    {"Inicio", "Firmar", "Validar", "Ajustes", "Acerca de"};
	for (int i = 0; i < 5; ++i) {
		gtk_list_box_select_row(
		    navigation,
		    gtk_list_box_get_row_at_index(navigation, i)
		);
		g_assert_cmpstr(gtk_stack_get_visible_child_name(stack), ==, ids[i]);
		GtkWidget* page = gtk_stack_get_visible_child(stack);
		GtkWidget* label = find_widget(page, GTK_TYPE_LABEL);
		g_assert_cmpstr(gtk_label_get_label(GTK_LABEL(label)), ==, titles[i]);
		g_assert_true(adw_overlay_split_view_get_show_sidebar(split));
	}
	gtk_list_box_select_row(
	    navigation,
	    gtk_list_box_get_row_at_index(navigation, 0)
	);
	settings_test(window, settings_directory);
	gtk_list_box_select_row(
	    navigation,
	    gtk_list_box_get_row_at_index(navigation, 3)
	);
	snapshot(window, directory, "settings.png");
	gtk_list_box_select_row(
	    navigation,
	    gtk_list_box_get_row_at_index(navigation, 0)
	);
	snapshot(window, directory, "wide.png");
	gtk_window_set_default_size(GTK_WINDOW(window), 400, 700);
	settle();
	g_assert_true(adw_overlay_split_view_get_collapsed(split));
	g_assert_false(adw_overlay_split_view_get_show_sidebar(split));
	g_assert_true(gtk_widget_get_visible(GTK_WIDGET(toggle)));
	g_assert_false(gtk_toggle_button_get_active(toggle));
	snapshot(window, directory, "narrow.png");
	gtk_toggle_button_set_active(toggle, TRUE);
	g_assert_true(adw_overlay_split_view_get_show_sidebar(split));
	snapshot(window, directory, "overlay.png");
	gtk_toggle_button_set_active(toggle, FALSE);
	g_assert_false(adw_overlay_split_view_get_show_sidebar(split));
	gtk_toggle_button_set_active(toggle, TRUE);
	g_signal_emit_by_name(
	    navigation,
	    "row-activated",
	    gtk_list_box_get_selected_row(navigation)
	);
	g_assert_false(adw_overlay_split_view_get_show_sidebar(split));
	for (int i = 1; i <= 5; ++i) {
		gtk_toggle_button_set_active(toggle, TRUE);
		gtk_list_box_select_row(
		    navigation,
		    gtk_list_box_get_row_at_index(navigation, i % 5)
		);
		g_assert_cmpstr(
		    gtk_stack_get_visible_child_name(stack),
		    ==,
		    ids[i % 5]
		);
		g_assert_false(adw_overlay_split_view_get_show_sidebar(split));
		g_assert_false(gtk_toggle_button_get_active(toggle));
	}
	gtk_window_set_default_size(GTK_WINDOW(window), 900, 600);
	settle();
	g_assert_false(adw_overlay_split_view_get_collapsed(split));
	g_assert_true(adw_overlay_split_view_get_show_sidebar(split));
	g_assert_false(gtk_widget_get_visible(GTK_WIDGET(toggle)));
	gtk_window_destroy(GTK_WINDOW(window));
	settle();
	g_object_unref(app);
	char* settings_path = config_path();
	char* settings_parent = g_path_get_dirname(settings_path);
	g_assert_cmpint(g_unlink(settings_path), ==, 0);
	g_assert_cmpint(g_rmdir(settings_parent), ==, 0);
	g_free(settings_path);
	g_free(settings_parent);
	g_assert_cmpint(g_rmdir(settings_directory), ==, 0);
	g_free(settings_directory);
	return 0;
}
