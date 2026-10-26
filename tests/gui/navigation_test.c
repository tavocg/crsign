/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "helpers.h"
#include <glib/gstdio.h>
#include <sign/config.h>
#include <sign/gui/config.h>
#include <sign/gui/devices.h>
#include <sign/gui/home.h>
#include <sign/gui/i18n.h>
#include <sign/gui/settings.h>
#include <sign/gui/signing.h>
#include <sign/gui/validation.h>
#include <sign/gui/window.h>
#include <sign/history.h>
#include <string.h>

static void set_mode(GtkWidget* settings, const char* key, guint index) {
	guint offset = adw_combo_row_get_selected(
	                   ADW_COMBO_ROW(named(settings, "settings-scope"))
	               ) == 0
	                   ? 1
	                   : 0;
	g_assert_cmpuint(index, >=, offset);
	adw_combo_row_set_selected(
	    ADW_COMBO_ROW(named(settings, key)),
	    index - offset
	);
}
static void settings_test(GtkWidget* window, const char* directory) {
	(void)directory;
	GtkWidget*    view = find_widget(window, SIGN_TYPE_SETTINGS);
	SignSettings* settings = SIGN_SETTINGS(view);
	char*         path = config_path();
	g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
	g_assert_false(gtk_widget_get_visible(named(view, "settings-status")));
	g_assert_null(named(view, "mime_type"));
	GtkWidget*   modules = named(view, "modules");
	AdwComboRow* global_mode = ADW_COMBO_ROW(named(view, "modules-mode"));
	g_assert_cmpuint(
	    g_list_model_get_n_items(adw_combo_row_get_model(global_mode)),
	    ==,
	    2
	);
	g_assert_cmpstr(
	    gtk_string_object_get_string(
	        adw_combo_row_get_selected_item(global_mode)
	    ),
	    ==,
	    "Auto"
	);
	g_assert_cmpuint(
	    adw_combo_row_get_selected(ADW_COMBO_ROW(named(view, "modules-mode"))),
	    ==,
	    0
	);
	g_assert_false(gtk_widget_get_visible(gtk_widget_get_parent(modules)));
	GtkWidget* token = named(view, "token");
	set_mode(view, "token-mode", 2);
	gtk_editable_set_text(GTK_EDITABLE(token), "pkcs11:token=Global");
	set_mode(view, "hash", 3); /* sha384 */
	for (int i = 0; i < 3; ++i) {
		settle();
	}
	g_assert_true(g_file_test(path, G_FILE_TEST_IS_REGULAR));
	g_assert_false(gtk_widget_get_visible(named(view, "settings-status")));
	set_mode(view, "modules-mode", 2);
	g_assert_true(gtk_widget_get_visible(gtk_widget_get_parent(modules)));
	g_assert_cmpint(
	    gtk_widget_get_margin_top(gtk_widget_get_parent(modules)),
	    >=,
	    12
	);
	g_assert_false(gtk_widget_get_visible(modules));
	g_signal_emit_by_name(named(view, "add-module"), "clicked");
	GtkWidget* first = gtk_widget_get_first_child(modules);
	gtk_editable_set_text(GTK_EDITABLE(first), "one.so");
	g_signal_emit_by_name(named(view, "add-module"), "clicked");
	GtkWidget* second = gtk_widget_get_last_child(modules);
	gtk_editable_set_text(GTK_EDITABLE(second), "two.so");
	g_assert_true(sign_settings_flush(settings));
	char      detail[512];
	sign_args args = {0};
	g_assert_true(config_load(&args, NULL, detail, sizeof(detail)));
	g_assert_cmpuint(args.options.modules.count, ==, 2);
	arg_free(&args);
	g_signal_emit_by_name(named(first, "remove-module"), "clicked");
	g_assert_true(sign_settings_flush(settings));
	g_assert_true(config_load(&args, NULL, detail, sizeof(detail)));
	g_assert_cmpuint(args.options.modules.count, ==, 1);
	g_assert_true(g_str_has_suffix(args.options.modules.items[0], "two.so"));
	arg_free(&args);
	g_signal_emit_by_name(named(view, "add-profile"), "clicked");
	gtk_editable_set_text(GTK_EDITABLE(named(view, "label")), "bccr");
	g_assert_cmpuint(
	    g_list_model_get_n_items(adw_combo_row_get_model(global_mode)),
	    ==,
	    3
	);
	g_assert_cmpstr(
	    gtk_string_object_get_string(
	        adw_combo_row_get_selected_item(global_mode)
	    ),
	    ==,
	    "Global"
	);
	gtk_editable_set_text(GTK_EDITABLE(named(view, "name")), "Banco ñ");
	g_assert_cmpuint(
	    adw_combo_row_get_selected(ADW_COMBO_ROW(named(view, "hash"))),
	    ==,
	    0
	);
	g_assert_true(sign_settings_flush(settings));
	g_assert_true(config_load(&args, "bccr", detail, sizeof(detail)));
	g_assert_cmpstr(args.options.uri, ==, "pkcs11:token=Global");
	g_assert_cmpint(args.options.hash, ==, LIBSSIG_HASH_SHA384);
	g_assert_cmpuint(args.options.modules.count, ==, 1);
	arg_free(&args);
	set_mode(view, "hash", 1);
	set_mode(view, "token-mode", 1);
	set_mode(view, "modules-mode", 1);
	g_assert_false(gtk_widget_get_visible(gtk_widget_get_parent(modules)));
	g_assert_true(sign_settings_flush(settings));
	g_assert_true(config_load(&args, "bccr", detail, sizeof(detail)));
	g_assert_null(args.options.uri);
	g_assert_cmpint(args.options.hash, ==, LIBSSIG_HASH_SHA256);
	g_assert_cmpuint(args.options.modules.count, ==, 0);
	arg_free(&args);
	GtkWidget* reopened = sign_settings_new();
	g_object_ref_sink(reopened);
	AdwComboRow* scopes = ADW_COMBO_ROW(named(reopened, "settings-scope"));
	g_assert_cmpuint(
	    g_list_model_get_n_items(adw_combo_row_get_model(scopes)),
	    ==,
	    2
	);
	adw_combo_row_set_selected(scopes, 1);
	GtkStringObject* display = adw_combo_row_get_selected_item(scopes);
	g_assert_cmpstr(gtk_string_object_get_string(display), ==, "Banco ñ");
	g_assert_cmpuint(
	    adw_combo_row_get_selected(ADW_COMBO_ROW(named(reopened, "hash"))),
	    ==,
	    1
	);
	g_assert_cmpuint(
	    adw_combo_row_get_selected(
	        ADW_COMBO_ROW(named(reopened, "token-mode"))
	    ),
	    ==,
	    1
	);
	g_assert_cmpuint(
	    adw_combo_row_get_selected(
	        ADW_COMBO_ROW(named(reopened, "modules-mode"))
	    ),
	    ==,
	    1
	);
	g_assert_false(gtk_widget_get_visible(named(reopened, "settings-status")));
	g_object_unref(reopened);
	gtk_editable_set_text(GTK_EDITABLE(named(view, "name")), "");
	g_assert_true(sign_settings_flush(settings));
	AdwComboRow* current = ADW_COMBO_ROW(named(view, "settings-scope"));
	g_assert_cmpstr(
	    gtk_string_object_get_string(adw_combo_row_get_selected_item(current)),
	    ==,
	    "bccr"
	);
	gtk_editable_set_text(GTK_EDITABLE(named(view, "name")), "Banco ñ");
	g_assert_true(sign_settings_flush(settings));
	set_mode(view, "token-mode", 0);
	set_mode(view, "hash", 0);
	set_mode(view, "modules-mode", 0);
	g_assert_true(sign_settings_flush(settings));
	g_assert_true(config_load(&args, "bccr", detail, sizeof(detail)));
	g_assert_cmpstr(args.options.uri, ==, "pkcs11:token=Global");
	g_assert_cmpint(args.options.hash, ==, LIBSSIG_HASH_SHA384);
	g_assert_cmpuint(args.options.modules.count, ==, 1);
	arg_free(&args);
	gtk_editable_set_text(GTK_EDITABLE(named(view, "label")), "");
	g_assert_false(sign_settings_flush(settings));
	g_assert_true(gtk_widget_get_visible(named(view, "settings-status")));
	gtk_editable_set_text(GTK_EDITABLE(named(view, "label")), "bccr");
	g_assert_true(sign_settings_flush(settings));
	g_assert_false(gtk_widget_get_visible(named(view, "settings-status")));
	g_free(path);
}
static void validate_context(
    GtkWidget* item,
    GtkWidget* window,
    GtkStack*  stack,
    int        return_section
) {
	SignValidation* validation = SIGN_VALIDATION(
	    gtk_stack_get_child_by_name(stack, "verify")
	);
	g_assert_cmpuint(sign_validation_count(validation), ==, 0);
	g_assert_true(gtk_widget_activate_action(item, "document.validate", NULL));
	g_assert_cmpstr(gtk_stack_get_visible_child_name(stack), ==, "verify");
	g_assert_cmpuint(sign_validation_count(validation), ==, 1);
	gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
	while (sign_validation_is_busy(validation) &&
	       g_get_monotonic_time() < deadline) {
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	}
	g_assert_false(sign_validation_is_busy(validation));
	g_signal_emit_by_name(sign_validation_header_clear(validation), "clicked");
	GtkListBox* navigation = GTK_LIST_BOX(named(window, "navigation"));
	gtk_list_box_select_row(
	    navigation,
	    gtk_list_box_get_row_at_index(navigation, return_section)
	);
}
int main(int argc, char** argv) {
	const char* directory = argc > 1 ? argv[1] : NULL;
	if (directory) {
		g_assert_cmpint(g_mkdir_with_parents(directory, 0700), ==, 0);
	}
	char* settings_directory = g_dir_make_tmp("crsign-navigation-XXXXXX", NULL);
	g_setenv("XDG_CONFIG_HOME", settings_directory, TRUE);
	char* data_directory = g_dir_make_tmp(
	    "crsign-navigation-data-XXXXXX",
	    NULL
	);
	g_setenv("XDG_DATA_HOME", data_directory, TRUE);
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
	g_assert_true(gtk_icon_theme_has_icon(
	    gtk_icon_theme_get_for_display(gtk_widget_get_display(window)),
	    "crsign-clear-symbolic"
	));
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
	g_assert_cmpstr(gtk_stack_get_visible_child_name(stack), ==, "sign");
	g_assert_false(gtk_widget_get_visible(named(window, "home-navigation")));
	char* seed = g_build_filename(data_directory, "initial-signed.pdf", NULL);
	g_assert_cmpint(g_mkdir_with_parents(data_directory, 0700), ==, 0);
	g_assert_true(g_file_set_contents(seed, "signed fixture", -1, NULL));
	g_assert_true(history_record(seed, NULL));
	g_free(seed);
	sign_home_reload(SIGN_HOME(gtk_stack_get_child_by_name(stack, "home")));
	g_assert_true(gtk_widget_get_visible(named(window, "home-navigation")));
	g_assert_nonnull(navigation);
	g_assert_null(gtk_list_box_get_row_at_index(navigation, 4));
	g_assert_nonnull(toggle);
	g_assert_cmpstr(
	    gtk_button_get_icon_name(GTK_BUTTON(toggle)),
	    ==,
	    "sidebar-show-symbolic"
	);
	g_assert_false(adw_overlay_split_view_get_collapsed(split));
	g_assert_true(adw_overlay_split_view_get_show_sidebar(split));
	g_assert_false(gtk_widget_get_visible(GTK_WIDGET(toggle)));
	const char* ids[] = {"home", "sign", "verify", "devices"};

	for (guint i = 0; i < G_N_ELEMENTS(ids); ++i) {
		gtk_list_box_select_row(
		    navigation,
		    gtk_list_box_get_row_at_index(navigation, i)
		);
		g_assert_cmpstr(gtk_stack_get_visible_child_name(stack), ==, ids[i]);
		GtkWidget* page = gtk_stack_get_visible_child(stack);
		if (i == 1) {
			g_assert_true(SIGN_IS_SIGNING(page));
			GtkDropDown* selector = GTK_DROP_DOWN(
			    named(window, "sign-profile")
			);
			g_assert_cmpint(
			    gtk_widget_get_visible(GTK_WIDGET(selector)),
			    ==,
			    g_list_model_get_n_items(gtk_drop_down_get_model(selector)) > 1
			);
			g_assert_nonnull(gtk_widget_get_ancestor(
			    sign_signing_header_clear(SIGN_SIGNING(page)),
			    ADW_TYPE_HEADER_BAR
			));

			g_assert_true(
			    gtk_widget_get_parent(named(window, "sign-profile")) ==
			    gtk_widget_get_parent(named(window, "sign-run"))
			);
			g_assert_false(gtk_widget_is_ancestor(
			    named(window, "sign-profile"),
			    named(window, "section-title")
			));
		} else if (i == 2) {
			g_assert_true(SIGN_IS_VALIDATION(page));
			g_assert_nonnull(gtk_widget_get_ancestor(
			    sign_validation_header_clear(SIGN_VALIDATION(page)),
			    ADW_TYPE_HEADER_BAR
			));
		} else if (i == 3) {
			g_assert_true(SIGN_IS_DEVICES(page));
		} else {
			g_assert_true(SIGN_IS_HOME(page));
			g_assert_nonnull(named(page, "home-recents"));
			g_assert_nonnull(named(page, "home-favorites"));
		}
		g_assert_true(adw_overlay_split_view_get_show_sidebar(split));
	}
	gtk_list_box_select_row(
	    navigation,
	    gtk_list_box_get_row_at_index(navigation, 0)
	);
	g_assert_true(gtk_widget_activate_action(window, "menu.settings", NULL));
	settle();
	settings_test(
	    GTK_WIDGET(adw_application_window_get_visible_dialog(
	        ADW_APPLICATION_WINDOW(window)
	    )),
	    settings_directory
	);
	AdwDialog* visible_settings = adw_application_window_get_visible_dialog(
	    ADW_APPLICATION_WINDOW(window)
	);
	if (visible_settings) {
		adw_dialog_force_close(visible_settings);
		settle();
	}
	g_assert_true(gtk_widget_activate_action(window, "menu.settings", NULL));
	settle();
	GtkWidget* settings_view = find_widget(
	    GTK_WIDGET(adw_application_window_get_visible_dialog(
	        ADW_APPLICATION_WINDOW(window)
	    )),
	    SIGN_TYPE_SETTINGS
	);
	gtk_widget_grab_focus(named(settings_view, "settings-scope"));
	snapshot(window, directory, "settings.png");
	adw_combo_row_set_selected(
	    ADW_COMBO_ROW(named(settings_view, "settings-scope")),
	    0
	);
	gtk_widget_grab_focus(named(settings_view, "modules-mode"));
	snapshot(window, directory, "modules.png");
	gtk_list_box_select_row(
	    navigation,
	    gtk_list_box_get_row_at_index(navigation, 0)
	);
	AdwDialog* visible_about = adw_application_window_get_visible_dialog(
	    ADW_APPLICATION_WINDOW(window)
	);
	if (visible_about) {
		adw_dialog_force_close(visible_about);
		settle();
	}
	g_assert_true(gtk_widget_activate_action(window, "menu.about", NULL));
	settle();
	snapshot(window, directory, "about.png");
	gtk_list_box_select_row(
	    navigation,
	    gtk_list_box_get_row_at_index(navigation, 0)
	);
	AdwDialog* about = adw_application_window_get_visible_dialog(
	    ADW_APPLICATION_WINDOW(window)
	);
	g_assert_nonnull(about);
	g_assert_cmpstr(
	    gtk_label_get_text(
	        GTK_LABEL(named(GTK_WIDGET(about), "about-version"))
	    ),
	    ==,
	    "CR Sign v" SIGN_GUI_VERSION
	);
	dismiss_dialog_background(window);
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
	for (int i = 1; i <= 6; ++i) {
		gtk_toggle_button_set_active(toggle, TRUE);
		gtk_list_box_select_row(
		    navigation,
		    gtk_list_box_get_row_at_index(navigation, i % 4)
		);
		g_assert_cmpstr(
		    gtk_stack_get_visible_child_name(stack),
		    ==,
		    ids[i % 4]
		);
		g_assert_false(adw_overlay_split_view_get_show_sidebar(split));
		g_assert_false(gtk_toggle_button_get_active(toggle));
	}
	gtk_window_set_default_size(GTK_WINDOW(window), 900, 600);
	settle();
	g_assert_false(adw_overlay_split_view_get_collapsed(split));
	g_assert_true(adw_overlay_split_view_get_show_sidebar(split));
	g_assert_false(gtk_widget_get_visible(GTK_WIDGET(toggle)));
	gtk_toggle_button_set_active(
	    GTK_TOGGLE_BUTTON(named(window, "home-view-toggle")),
	    TRUE
	);
	gtk_window_destroy(GTK_WINDOW(window));
	settle();
	char* output = g_build_filename(data_directory, "signed.pdf", NULL);
	g_assert_true(g_file_set_contents(output, "signed fixture", -1, NULL));
	g_assert_true(history_record(output, NULL));
	GtkWidget* reopened = sign_window_new(GTK_APPLICATION(app));
	GtkStack* reopened_stack = GTK_STACK(find_widget(reopened, GTK_TYPE_STACK));
	g_assert_cmpstr(
	    gtk_stack_get_visible_child_name(reopened_stack),
	    ==,
	    "home"
	);
	g_assert_true(gtk_toggle_button_get_active(
	    GTK_TOGGLE_BUTTON(named(reopened, "home-view-toggle"))
	));
	SignHome* home = SIGN_HOME(
	    gtk_stack_get_child_by_name(reopened_stack, "home")
	);
	g_assert_false(
	    gtk_widget_get_visible(named(GTK_WIDGET(home), "home-favorites"))
	);
	g_assert_true(
	    gtk_widget_get_visible(named(GTK_WIDGET(home), "home-recents"))
	);
	GtkListBox* home_files = GTK_LIST_BOX(
	    find_widget(named(GTK_WIDGET(home), "home-recents"), GTK_TYPE_LIST_BOX)
	);
	validate_context(
	    GTK_WIDGET(gtk_list_box_get_row_at_index(home_files, 0)),
	    reopened,
	    reopened_stack,
	    0
	);
	GtkMenuButton* search_button = GTK_MENU_BUTTON(
	    sign_home_search_button(home)
	);
	gtk_menu_button_popup(search_button);
	settle();
	GtkWidget* search_popup = GTK_WIDGET(
	    gtk_menu_button_get_popover(search_button)
	);
	GtkListBox* search_results = GTK_LIST_BOX(
	    named(search_popup, "history-search-results")
	);
	validate_context(
	    GTK_WIDGET(gtk_list_box_get_row_at_index(search_results, 0)),
	    reopened,
	    reopened_stack,
	    0
	);
	settle();
	g_assert_false(gtk_widget_get_visible(search_popup));
	gtk_menu_button_popup(search_button);
	settle();
	g_assert_true(gtk_widget_get_visible(search_popup));
	gtk_menu_button_popdown(search_button);
	settle();
	g_assert_false(gtk_widget_get_visible(search_popup));
	char* emptied = history_path();
	g_assert_cmpint(g_unlink(emptied), ==, 0);
	g_free(emptied);
	sign_home_reload(home);
	g_assert_false(gtk_widget_get_visible(named(reopened, "home-navigation")));
	g_assert_cmpstr(
	    gtk_stack_get_visible_child_name(reopened_stack),
	    ==,
	    "sign"
	);
	/* Successful signing switches sections and empties the original queue. */
	g_assert_true(
	    g_file_set_contents(output, "signed output fixture", -1, NULL)
	);
	SignSigning* signing = SIGN_SIGNING(
	    gtk_stack_get_child_by_name(reopened_stack, "sign")
	);
	GListStore* pending = g_list_store_new(G_TYPE_FILE);
	GFile*      file = g_file_new_for_path(output);
	g_list_store_append(pending, file);
	sign_signing_add_files(signing, G_LIST_MODEL(pending));
	g_object_unref(file);
	g_object_unref(pending);
	GtkListBox* signing_files = GTK_LIST_BOX(
	    named(GTK_WIDGET(signing), "sign-documents")
	);
	validate_context(
	    GTK_WIDGET(gtk_list_box_get_row_at_index(signing_files, 0)),
	    reopened,
	    reopened_stack,
	    1
	);
	g_assert_cmpuint(sign_signing_count(signing), ==, 1);
	sign_flow_item item = {.operation = SIGN_FLOW_VERIFY};
	item.options.input = output;
	sign_flow* verification = sign_flow_from_items(&item, 1, &error);
	g_assert_no_error(error);
	g_signal_emit_by_name(signing, "signing-completed", verification);
	sign_flow_free(verification);
	g_assert_cmpstr(
	    gtk_stack_get_visible_child_name(reopened_stack),
	    ==,
	    "verify"
	);
	g_assert_cmpuint(sign_signing_count(signing), ==, 0);
	SignValidation* validation = SIGN_VALIDATION(
	    gtk_stack_get_child_by_name(reopened_stack, "verify")
	);
	g_assert_cmpuint(sign_validation_count(validation), ==, 1);
	gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
	while (sign_validation_is_busy(validation) &&
	       g_get_monotonic_time() < deadline) {
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	}
	g_assert_false(sign_validation_is_busy(validation));
	g_unlink(output);
	gtk_window_destroy(GTK_WINDOW(reopened));
	settle();
	char* path = history_path();
	g_unlink(path);
	g_free(path);
	char* preferences = g_build_filename(
	    data_directory,
	    "crsign",
	    "preferences.json",
	    NULL
	);
	g_unlink(preferences);
	g_free(preferences);
	char* lock = g_build_filename(data_directory, "crsign", "state.lock", NULL);
	g_unlink(lock);
	g_free(lock);
	char* data_parent = g_build_filename(data_directory, "crsign", NULL);
	g_rmdir(data_parent);
	g_free(data_parent);
	g_rmdir(data_directory);
	g_free(data_directory);
	g_free(output);
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
