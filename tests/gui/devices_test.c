/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "helpers.h"
#include <sign/config.h>
#include <sign/gui/devices.h>
#include <sign/gui/inventory.h>
#include <sign/gui/window.h>
#include <sign/ui_preferences.h>

static guint widget_count(GtkWidget* widget) {
	guint count = 1;
	for (GtkWidget* child = gtk_widget_get_first_child(widget); child;
	     child = gtk_widget_get_next_sibling(child)) {
		count += widget_count(child);
	}
	return count;
}
static void clipboard_read(
    GObject*      source,
    GAsyncResult* result,
    gpointer      data
) {
	GError* error = NULL;
	char*   text = gdk_clipboard_read_text_finish(
        GDK_CLIPBOARD(source),
        result,
        &error
    );
	g_assert_no_error(error);
	g_assert_nonnull(text);
	g_assert_true(g_str_has_prefix(text, "pkcs11:"));
	g_free(text);
	*(bool*)data = true;
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
static void wait_text(GtkWidget* view, const char* text, bool present) {
	gint64 deadline = g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND;
	while ((contains(view, text) != present ||
	        sign_devices_is_busy(SIGN_DEVICES(view))) &&
	       g_get_monotonic_time() < deadline) {
		while (g_main_context_iteration(NULL, FALSE)) {
		}
		g_usleep(1000);
	}
	g_assert_cmpint(contains(view, text), ==, present);
	g_assert_false(sign_devices_is_busy(SIGN_DEVICES(view)));
}
static void wait_card(GtkWidget* view, const char* serial) {
	gint64 deadline = g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND;
	do {
		while (g_main_context_iteration(NULL, FALSE)) {
		}
		GtkWidget* slot = named(view, "devices-slot");
		if (slot) {
			adw_expander_row_set_expanded(ADW_EXPANDER_ROW(slot), TRUE);
		}
		if (contains(view, serial) &&
		    !sign_devices_is_busy(SIGN_DEVICES(view))) {
			return;
		}
		g_usleep(1000);
	} while (g_get_monotonic_time() < deadline);
	g_error("Card serial did not appear: %s", serial);
}
static void phase(const char* root, const char* value) {
	char* path = g_build_filename(root, "phase", NULL);
	g_assert_true(g_file_set_contents(path, value, -1, NULL));
	g_free(path);
}
static void trust_test(void) {
	sign_args settings = {0};
	char      detail[512];
	g_assert_true(config_load(&settings, NULL, detail, sizeof(detail)));
	libssig_context context = {0};
	g_assert_cmpint(
	    libssig_context_open_modules(&context, &settings.options.modules),
	    ==,
	    LIBSSIG_OK
	);
	GCancellable*          cancel = g_cancellable_new();
	GError*                error = NULL;
	sign_device_inventory* inventory = sign_device_inventory_scan(
	    &context,
	    &settings.options,
	    0,
	    cancel,
	    &error
	);
	g_assert_no_error(error);
	g_assert_nonnull(inventory);
	libssig_certificate*
	    cert = &inventory->devices.items[0].slots[0].certificates.items[0];
	sign_device_certificate* annotation = g_hash_table_lookup(
	    inventory->certificates,
	    cert
	);
	g_assert_cmpint(annotation->system_trust, ==, X509_V_OK);
	g_assert_cmpint(annotation->application_trust, ==, X509_V_OK);
	sign_device_inventory_free(inventory);
	/* The synthetic embedded alpha root is the only application anchor. */
	settings.options.no_bundled_certs = true;
	inventory = sign_device_inventory_scan(
	    &context,
	    &settings.options,
	    0,
	    cancel,
	    &error
	);
	g_assert_no_error(error);
	g_assert_nonnull(inventory);
	cert = &inventory->devices.items[0].slots[0].certificates.items[0];
	annotation = g_hash_table_lookup(inventory->certificates, cert);
	g_assert_cmpint(annotation->application_trust, !=, X509_V_OK);
	sign_device_inventory_free(inventory);
	libssig_context_close(&context);
	arg_free(&settings);
	g_object_unref(cancel);
}
int main(int argc, char** argv) {
	g_assert_cmpint(argc, ==, 3);
	GLogLevelFlags fatal = g_log_set_always_fatal(G_LOG_FATAL_MASK);
	g_log_set_always_fatal(fatal | G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL);
	trust_test();
	adw_init();
	adw_style_manager_set_color_scheme(
	    adw_style_manager_get_default(),
	    ADW_COLOR_SCHEME_FORCE_DARK
	);
	g_assert_cmpint(g_mkdir_with_parents(argv[2], 0700), ==, 0);
	AdwApplication* app = adw_application_new(
	    "org.crsign.Sign.DevicesTest",
	    G_APPLICATION_NON_UNIQUE
	);
	g_assert_true(g_application_register(G_APPLICATION(app), NULL, NULL));
	GtkWidget* window = sign_window_new(GTK_APPLICATION(app));
	GtkWidget* view = find_widget(window, SIGN_TYPE_DEVICES);
	g_assert_true(sign_devices_is_busy(SIGN_DEVICES(view)));
	g_assert_true(gtk_spinner_get_spinning(
	    GTK_SPINNER(named(window, "devices-nav-spinner"))
	));
	gtk_window_present(GTK_WINDOW(window));
	wait_text(view, "Signing card <&>", true);
	/* Re-entering the section must not restart the scan; only Refresh does. */
	GtkListBox* navigation = GTK_LIST_BOX(named(window, "navigation"));
	gtk_list_box_select_row(
	    navigation,
	    gtk_list_box_get_row_at_index(navigation, 3)
	);
	g_assert_false(sign_devices_is_busy(SIGN_DEVICES(view)));
	g_assert_false(contains(view, "2048 bits"));
	g_assert_null(named(view, "devices-certificate"));
	g_assert_false(contains(view, "Software fixture"));
	g_assert_false(gtk_spinner_get_spinning(
	    GTK_SPINNER(named(window, "devices-nav-spinner"))
	));
	GtkWidget*    content = named(view, "devices-content");
	AdwActionRow* reader = ADW_ACTION_ROW(named(content, "devices-reader"));
	g_assert_nonnull(reader);
	g_assert_null(find_widget(content, ADW_TYPE_EXPANDER_ROW));
	AdwNavigationSplitView* split = ADW_NAVIGATION_SPLIT_VIEW(
	    named(view, "devices-split")
	);
	gtk_window_set_default_size(GTK_WINDOW(window), 1120, 800);
	settle();
	g_assert_false(adw_navigation_split_view_get_collapsed(split));
	g_assert_false(adw_navigation_split_view_get_show_content(split));

	navigation = GTK_LIST_BOX(named(window, "navigation"));
	gtk_list_box_select_row(
	    navigation,
	    gtk_list_box_get_row_at_index(navigation, 3)
	);
	wait_text(view, "Signing card <&>", true);
	snapshot(window, argv[2], "devices-collapsed.png");
	graphene_rect_t options, title;
	g_assert_true(gtk_widget_compute_bounds(
	    named(window, "devices-options"),
	    window,
	    &options
	));
	g_assert_true(gtk_widget_compute_bounds(
	    named(window, "section-title"),
	    window,
	    &title
	));
	g_assert_cmpfloat(options.origin.x + options.size.width, <, title.origin.x);

	reader = ADW_ACTION_ROW(named(content, "devices-reader"));
	g_signal_emit_by_name(reader, "activated");
	settle();
	g_assert_true(adw_navigation_split_view_get_show_content(split));
	snapshot(window, argv[2], "devices-expanded.png");
	AdwExpanderRow* slot = ADW_EXPANDER_ROW(named(view, "devices-slot"));
	guint           collapsed = widget_count(view);
	g_assert_false(adw_expander_row_get_expanded(slot));
	GBytes* collapsed_arrow = arrow_shape(window, GTK_WIDGET(slot));
	adw_expander_row_set_expanded(slot, TRUE);
	g_assert_true(adw_expander_row_get_expanded(slot));
	GBytes* expanded_arrow = arrow_shape(window, GTK_WIDGET(slot));
	g_assert_false(g_bytes_equal(collapsed_arrow, expanded_arrow));
	adw_expander_row_set_expanded(slot, FALSE);
	GBytes* closed_again = arrow_shape(window, GTK_WIDGET(slot));
	g_assert_true(g_bytes_equal(collapsed_arrow, closed_again));
	g_bytes_unref(collapsed_arrow);
	g_bytes_unref(expanded_arrow);
	g_bytes_unref(closed_again);
	snapshot(window, argv[2], "devices-slot-collapsed.png");
	adw_expander_row_set_expanded(slot, TRUE);
	g_assert_true(contains(view, "2048 bits"));
	g_assert_true(contains(view, "3072 bits"));
	g_assert_true(contains(view, "256 bits"));
	g_assert_cmpuint(widget_count(view), >, collapsed);
	GtkWidget* certificate = named(view, "devices-certificate");
	g_assert_nonnull(certificate);
	g_assert_true(ADW_IS_ACTION_ROW(certificate));
	g_assert_true(contains(view, "CN=Device fixture signer"));
	no_nested_expanders(view, false);
	gtk_window_set_default_size(GTK_WINDOW(window), 440, 750);
	settle();
	g_assert_true(adw_navigation_split_view_get_collapsed(split));
	GtkWidget* back = named(view, "details-back");
	g_assert_true(gtk_widget_get_visible(back));
	snapshot(window, argv[2], "devices-details-narrow.png");
	g_signal_emit_by_name(back, "clicked");
	settle();
	g_assert_false(adw_navigation_split_view_get_show_content(split));
	g_signal_emit_by_name(reader, "activated");
	settle();
	g_assert_true(adw_navigation_split_view_get_show_content(split));
	gtk_window_set_default_size(GTK_WINDOW(window), 1120, 800);
	settle();
	g_assert_false(adw_navigation_split_view_get_collapsed(split));
	g_assert_false(gtk_widget_get_visible(back));
	GtkWidget* copy = named(view, "devices-copy-token-uri");
	g_assert_nonnull(copy);
	g_signal_emit_by_name(copy, "clicked");
	bool copied = false;
	gdk_clipboard_read_text_async(
	    gtk_widget_get_clipboard(copy),
	    NULL,
	    clipboard_read,
	    &copied
	);
	gint64 copy_deadline = g_get_monotonic_time() + 3 * G_TIME_SPAN_SECOND;
	while (!copied && g_get_monotonic_time() < copy_deadline) {
		while (g_main_context_iteration(NULL, FALSE)) {
		}
		g_usleep(1000);
	}
	g_assert_true(copied);

	GtkCheckButton* hardware = GTK_CHECK_BUTTON(
	    named(window, "devices-hardware-only")
	);
	g_assert_true(gtk_check_button_get_active(hardware));
	/* A superseded software scan must not reappear after switching it off. */
	for (unsigned n = 0; n < 6; ++n) {
		gtk_check_button_set_active(hardware, n % 2 != 0);
		while (g_main_context_iteration(NULL, FALSE)) {
		}
	}
	wait_text(view, "Software fixture", false);
	gtk_check_button_set_active(hardware, FALSE);
	wait_text(view, "Software fixture", true);
	bool saved_hardware = true;
	g_assert_true(ui_preferences_hardware_only(&saved_hardware, NULL));
	g_assert_false(saved_hardware);
	GtkWidget* restored = g_object_ref_sink(sign_devices_new());
	g_assert_false(gtk_check_button_get_active(GTK_CHECK_BUTTON(named(
	    sign_devices_header_menu(SIGN_DEVICES(restored)),
	    "devices-hardware-only"
	))));
	g_object_unref(restored);

	gtk_check_button_set_active(hardware, TRUE);
	wait_text(view, "Software fixture", false);
	phase(argv[1], "2");
	wait_card(view, "card-two");
	phase(argv[1], "1");
	wait_text(view, "Certificate <&>", false);
	phase(argv[1], "3");
	wait_text(view, "No readers found", true);
	g_assert_false(adw_navigation_split_view_get_show_content(split));
	g_assert_cmpint(
	    gtk_widget_get_valign(named(view, "devices-body")),
	    ==,
	    GTK_ALIGN_START
	);
	g_assert_cmpstr(
	    gtk_button_get_label(GTK_BUTTON(named(view, "devices-refresh"))),
	    ==,
	    "Refresh"
	);
	snapshot(window, argv[2], "devices-empty.png");
	phase(argv[1], "0");
	wait_text(view, "Signing card <&>", true);
	g_signal_emit_by_name(named(view, "devices-refresh"), "clicked");
	wait_text(view, "Signing card <&>", true);
	gpointer weak = view;
	g_object_add_weak_pointer(G_OBJECT(view), &weak);
	gtk_window_destroy(GTK_WINDOW(window));
	gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
	while (weak && g_get_monotonic_time() < deadline) {
		while (g_main_context_iteration(NULL, FALSE)) {
		}
		g_usleep(1000);
	}
	g_assert_null(weak);
	g_object_unref(app);
	return 0;
}
