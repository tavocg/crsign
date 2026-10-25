/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/gui/about.h>
#include <sign/gui/devices.h>
#include <sign/gui/error.h>
#include <sign/gui/home.h>
#include <sign/gui/i18n.h>
#include <sign/gui/settings.h>
#include <sign/gui/signing.h>
#include <sign/gui/validation.h>
#include <sign/gui/window.h>
#include <string.h>

/* Stable IDs stay independent of the translated labels. */
static const struct {
	const char* id;
	const char* title;
	const char* icon;
} sections[] = {
    {"home", N_("Home"), "user-home-symbolic"},
    {"sign", N_("Sign"), "document-edit-symbolic"},
    {"verify", N_("Validate"), "object-select-symbolic"},
    {"devices", N_("Devices"), "drive-removable-media-symbolic"},
    {"settings", N_("Settings"), "emblem-system-symbolic"},
    {"about", N_("About"), "help-about-symbolic"},
};

struct _SignWindow {
	AdwApplicationWindow parent_instance;
	AdwOverlaySplitView* split_view;
	GtkStack*            pages;
	GtkLabel*            title;
	GtkWidget *profile_selector, *view_toggle, *home_row, *devices_menu,
	    *details_back;
	SignHome* home;
};

G_DEFINE_TYPE(SignWindow, sign_window, ADW_TYPE_APPLICATION_WINDOW)

static void devices_busy_changed(
    SignDevices* devices,
    GParamSpec*  property,
    gpointer     data
) {
	(void)property;
	GtkStack*  icons = GTK_STACK(data);
	bool       busy = sign_devices_is_busy(devices);
	GtkWidget* spinner = gtk_stack_get_child_by_name(icons, "loading");
	if (busy) {
		gtk_spinner_start(GTK_SPINNER(spinner));
	} else {
		gtk_spinner_stop(GTK_SPINNER(spinner));
	}
	gtk_stack_set_visible_child_name(icons, busy ? "loading" : "icon");
}

static void device_navigation(SignDevices* devices, gpointer data) {
	SignWindow* self = data;
	if (g_strcmp0(gtk_stack_get_visible_child_name(self->pages), "devices")) {
		return;
	}
	bool details = sign_devices_in_details(devices);
	gtk_label_set_text(self->title, sign_devices_title(devices));
	gtk_widget_set_visible(self->details_back, details);
	gtk_widget_set_visible(self->devices_menu, !details);
}
static void device_back(GtkButton* button, gpointer data) {
	(void)button;
	SignWindow* self = data;
	sign_devices_back(
	    SIGN_DEVICES(gtk_stack_get_child_by_name(self->pages, "devices"))
	);
}
static void section_selected(
    GtkListBox*    list,
    GtkListBoxRow* row,
    gpointer       data
) {
	(void)list;
	if (!row) {
		return;
	}
	SignWindow* self = SIGN_WINDOW(data);
	int         index = gtk_list_box_row_get_index(row);
	if (index < 0 || (guint)index >= G_N_ELEMENTS(sections)) {
		return;
	}
	if (!strcmp(sections[index].id, "verify") ||
	    !strcmp(sections[index].id, "devices") ||
	    !strcmp(sections[index].id, "sign")) {
		/* Apply valid pending preferences before either worker reads its
		 * configuration. Invalid drafts remain unsaved. */
		sign_settings_flush(
		    SIGN_SETTINGS(gtk_stack_get_child_by_name(self->pages, "settings"))
		);
	}
	if (index == 0) {
		sign_home_reload(self->home);
		if (!sign_home_has_documents(self->home)) {
			gtk_list_box_select_row(
			    list,
			    gtk_list_box_get_row_at_index(list, 1)
			);
			return;
		}
	}
	if (index == 2) {
		sign_validation_prune(
		    SIGN_VALIDATION(gtk_stack_get_child_by_name(self->pages, "verify"))
		);
	}
	gtk_stack_set_visible_child_name(self->pages, sections[index].id);
	/* Device polling runs continuously. Selecting the section only reveals the
	 * current snapshot; an explicit Refresh rebuilds it. */
	gtk_widget_set_visible(self->view_toggle, index == 0);
	gtk_widget_set_visible(self->devices_menu, index == 3);
	bool signing = !strcmp(sections[index].id, "sign");
	gtk_widget_set_visible(self->profile_selector, signing);
	if (signing) {
		sign_signing_reload_profiles(
		    SIGN_SIGNING(gtk_stack_get_child_by_name(self->pages, "sign"))
		);
	}
	gtk_label_set_label(self->title, _(sections[index].title));
	gtk_widget_set_visible(self->details_back, FALSE);
	if (index == 3) {
		device_navigation(
		    SIGN_DEVICES(gtk_stack_get_child_by_name(self->pages, "devices")),
		    self
		);
	}
	if (adw_overlay_split_view_get_collapsed(self->split_view)) {
		adw_overlay_split_view_set_show_sidebar(self->split_view, FALSE);
	}
}

static GtkWidget* create_sidebar(SignWindow* self) {
	GtkWidget* toolbar = adw_toolbar_view_new();
	GtkWidget* header = adw_header_bar_new();
	GtkWidget* title = gtk_label_new(_("Menu"));
	gtk_widget_add_css_class(title, "title");
	adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), title);
	adw_header_bar_pack_start(
	    ADW_HEADER_BAR(header),
	    sign_home_search_button(self->home)
	);
	adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);

	GtkWidget* scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(
	    GTK_SCROLLED_WINDOW(scroll),
	    GTK_POLICY_NEVER,
	    GTK_POLICY_AUTOMATIC
	);
	GtkWidget* list = gtk_list_box_new();
	gtk_widget_set_name(list, "navigation");
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
	gtk_widget_add_css_class(list, "navigation-sidebar");
	for (guint i = 0; i < G_N_ELEMENTS(sections); ++i) {
		GtkWidget* row = gtk_list_box_row_new();
		if (i == 0) {
			self->home_row = row;
			gtk_widget_set_name(row, "home-navigation");
			gtk_widget_set_visible(row, sign_home_has_documents(self->home));
		}
		GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
		gtk_widget_set_margin_start(box, 12);
		gtk_widget_set_margin_end(box, 12);
		gtk_widget_set_margin_top(box, 10);
		gtk_widget_set_margin_bottom(box, 10);
		GtkWidget* icon = gtk_image_new_from_icon_name(sections[i].icon);
		if (!strcmp(sections[i].id, "devices")) {
			GtkWidget* icons = gtk_stack_new();
			gtk_widget_set_size_request(icons, 16, 16);
			gtk_stack_add_named(GTK_STACK(icons), icon, "icon");
			GtkWidget* spinner = gtk_spinner_new();
			gtk_widget_set_name(spinner, "devices-nav-spinner");
			gtk_stack_add_named(GTK_STACK(icons), spinner, "loading");
			SignDevices* devices = SIGN_DEVICES(
			    gtk_stack_get_child_by_name(self->pages, "devices")
			);
			g_signal_connect_object(
			    devices,
			    "notify::busy",
			    G_CALLBACK(devices_busy_changed),
			    icons,
			    0
			);
			devices_busy_changed(devices, NULL, icons);
			icon = icons;
		}
		gtk_box_append(GTK_BOX(box), icon);
		GtkWidget* label = gtk_label_new(_(sections[i].title));
		gtk_label_set_xalign(GTK_LABEL(label), 0);
		gtk_box_append(GTK_BOX(box), label);
		gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
		gtk_list_box_append(GTK_LIST_BOX(list), row);
	}
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
	adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), scroll);
	g_signal_connect(list, "row-selected", G_CALLBACK(section_selected), self);
	g_signal_connect(list, "row-activated", G_CALLBACK(section_selected), self);
	gtk_list_box_select_row(
	    GTK_LIST_BOX(list),
	    gtk_list_box_get_row_at_index(
	        GTK_LIST_BOX(list),
	        sign_home_has_documents(self->home) ? 0 : 1
	    )
	);
	return toolbar;
}

static void view_changed(GtkToggleButton* button, gpointer data) {
	SignWindow* self = data;
	bool        grid = !gtk_toggle_button_get_active(button);
	gtk_button_set_icon_name(
	    GTK_BUTTON(button),
	    grid ? "view-list-symbolic" : "view-grid-symbolic"
	);
	gtk_widget_set_tooltip_text(
	    GTK_WIDGET(button),
	    grid ? _("Show list") : _("Show grid")
	);
	sign_home_set_grid(self->home, grid);
}
static void signed_documents(SignSigning* signing, gpointer data) {
	(void)signing;
	sign_home_reload(SIGN_HOME(data));
}
static void signing_completed(
    SignSigning*     signing,
    const sign_flow* flow,
    gpointer         data
) {
	SignWindow*     self = data;
	SignValidation* validation = SIGN_VALIDATION(
	    gtk_stack_get_child_by_name(self->pages, "verify")
	);
	GError* error = NULL;
	if (!sign_validation_add_flow(validation, flow, &error)) {
		char* text = gui_error_message(error, GUI_ERROR_VALIDATION);
		gui_error_dialog(
		    GTK_WINDOW(self),
		    _("Could not validate signed documents"),
		    text
		);
		g_free(text);
		g_clear_error(&error);
		return;
	}
	sign_signing_clear(signing);
	GtkListBox* navigation = GTK_LIST_BOX(
	    gtk_widget_get_parent(self->home_row)
	);
	gtk_list_box_select_row(
	    navigation,
	    gtk_list_box_get_row_at_index(navigation, 2)
	);
}
static void validate_file(
    GSimpleAction* action,
    GVariant*      parameter,
    gpointer       data
) {
	(void)action;
	SignWindow* self = data;
	GtkListBox* navigation = GTK_LIST_BOX(
	    gtk_widget_get_parent(self->home_row)
	);
	gtk_list_box_select_row(
	    navigation,
	    gtk_list_box_get_row_at_index(navigation, 2)
	);
	GFile* file = g_file_new_for_uri(g_variant_get_string(parameter, NULL));
	GListStore* files = g_list_store_new(G_TYPE_FILE);
	g_list_store_append(files, file);
	sign_validation_add_files(
	    SIGN_VALIDATION(gtk_stack_get_child_by_name(self->pages, "verify")),
	    G_LIST_MODEL(files)
	);
	g_object_unref(files);
	g_object_unref(file);
}
static GtkWidget* create_content(SignWindow* self) {
	GtkWidget* toolbar = adw_toolbar_view_new();
	GtkWidget* header = adw_header_bar_new();
	self->title = GTK_LABEL(gtk_label_new(_("Home")));
	gtk_label_set_ellipsize(self->title, PANGO_ELLIPSIZE_END);
	gtk_label_set_max_width_chars(self->title, 40);
	gtk_widget_set_name(GTK_WIDGET(self->title), "section-title");
	gtk_widget_add_css_class(GTK_WIDGET(self->title), "title");
	adw_header_bar_set_title_widget(
	    ADW_HEADER_BAR(header),
	    GTK_WIDGET(self->title)
	);

	GtkWidget* toggle = gtk_toggle_button_new();
	gtk_widget_set_name(toggle, "menu-toggle");
	gtk_button_set_icon_name(GTK_BUTTON(toggle), "sidebar-show-symbolic");
	gtk_widget_set_tooltip_text(toggle, _("Toggle menu"));
	gtk_accessible_update_property(
	    GTK_ACCESSIBLE(toggle),
	    GTK_ACCESSIBLE_PROPERTY_LABEL,
	    _("Toggle menu"),
	    -1
	);
	g_object_bind_property(
	    self->split_view,
	    "collapsed",
	    toggle,
	    "visible",
	    G_BINDING_SYNC_CREATE
	);
	g_object_bind_property(
	    self->split_view,
	    "show-sidebar",
	    toggle,
	    "active",
	    G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL
	);
	adw_header_bar_pack_start(ADW_HEADER_BAR(header), toggle);
	self->details_back = gtk_button_new_from_icon_name("go-previous-symbolic");
	gtk_widget_set_name(self->details_back, "details-back");
	gtk_widget_set_tooltip_text(self->details_back, _("Back"));
	gtk_widget_set_visible(self->details_back, FALSE);
	g_signal_connect(
	    self->details_back,
	    "clicked",
	    G_CALLBACK(device_back),
	    self
	);
	adw_header_bar_pack_start(ADW_HEADER_BAR(header), self->details_back);
	self->view_toggle = gtk_toggle_button_new();
	gtk_button_set_icon_name(
	    GTK_BUTTON(self->view_toggle),
	    "view-list-symbolic"
	);
	gtk_widget_set_name(self->view_toggle, "home-view-toggle");
	gtk_widget_set_tooltip_text(self->view_toggle, _("Show list"));
	g_signal_connect(
	    self->view_toggle,
	    "toggled",
	    G_CALLBACK(view_changed),
	    self
	);
	adw_header_bar_pack_end(ADW_HEADER_BAR(header), self->view_toggle);
	adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);

	self->pages = GTK_STACK(gtk_stack_new());
	gtk_stack_set_transition_type(
	    self->pages,
	    GTK_STACK_TRANSITION_TYPE_CROSSFADE
	);
	gtk_stack_set_transition_duration(self->pages, 150);
	for (guint i = 0; i < G_N_ELEMENTS(sections); ++i) {
		if (!strcmp(sections[i].id, "home")) {
			self->home = SIGN_HOME(sign_home_new());
			gtk_stack_add_titled(
			    self->pages,
			    GTK_WIDGET(self->home),
			    "home",
			    _("Home")
			);
			continue;
		}
		if (!strcmp(sections[i].id, "sign")) {
			GtkWidget* signing = sign_signing_new();
			g_signal_connect_object(
			    signing,
			    "signing-completed",
			    G_CALLBACK(signing_completed),
			    self,
			    0
			);
			g_signal_connect_object(
			    signing,
			    "documents-signed",
			    G_CALLBACK(signed_documents),
			    self->home,
			    0
			);
			self->profile_selector = sign_signing_profile_selector(
			    SIGN_SIGNING(signing)
			);
			adw_header_bar_pack_start(
			    ADW_HEADER_BAR(header),
			    self->profile_selector
			);
			gtk_widget_set_visible(self->profile_selector, FALSE);
			gtk_stack_add_titled(
			    self->pages,
			    signing,
			    sections[i].id,
			    _(sections[i].title)
			);
			continue;
		}
		if (!strcmp(sections[i].id, "verify")) {
			gtk_stack_add_titled(
			    self->pages,
			    sign_validation_new(),
			    sections[i].id,
			    _(sections[i].title)
			);
			continue;
		}
		if (!strcmp(sections[i].id, "devices")) {
			GtkWidget* devices = sign_devices_new();
			g_signal_connect_object(
			    devices,
			    "navigation-changed",
			    G_CALLBACK(device_navigation),
			    self,
			    0
			);
			self->devices_menu = sign_devices_header_menu(
			    SIGN_DEVICES(devices)
			);
			adw_header_bar_pack_start(
			    ADW_HEADER_BAR(header),
			    self->devices_menu
			);
			gtk_widget_set_visible(self->devices_menu, FALSE);
			gtk_stack_add_titled(
			    self->pages,
			    devices,
			    sections[i].id,
			    _(sections[i].title)
			);
			continue;
		}
		if (!strcmp(sections[i].id, "settings")) {
			gtk_stack_add_titled(
			    self->pages,
			    sign_settings_new(),
			    sections[i].id,
			    _(sections[i].title)
			);
			continue;
		}
		if (!strcmp(sections[i].id, "about")) {
			gtk_stack_add_titled(
			    self->pages,
			    sign_about_new(),
			    sections[i].id,
			    _(sections[i].title)
			);
			continue;
		}
		GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		gtk_widget_set_halign(page, GTK_ALIGN_CENTER);
		gtk_widget_set_valign(page, GTK_ALIGN_CENTER);
		gtk_widget_set_margin_start(page, 24);
		gtk_widget_set_margin_end(page, 24);
		GtkWidget* label = gtk_label_new(_(sections[i].title));
		gtk_widget_add_css_class(label, "title-1");
		gtk_box_append(GTK_BOX(page), label);
		gtk_stack_add_titled(
		    self->pages,
		    page,
		    sections[i].id,
		    _(sections[i].title)
		);
	}
	gtk_toggle_button_set_active(
	    GTK_TOGGLE_BUTTON(self->view_toggle),
	    !sign_home_is_grid(self->home)
	);
	adw_toolbar_view_set_content(
	    ADW_TOOLBAR_VIEW(toolbar),
	    GTK_WIDGET(self->pages)
	);
	return toolbar;
}

static void home_history_changed(SignHome* home, gpointer data) {
	SignWindow* self = data;
	bool        available = sign_home_has_documents(home);
	bool        was_home = !g_strcmp0(
        gtk_stack_get_visible_child_name(self->pages),
        "home"
    );
	gtk_widget_set_visible(self->home_row, available);
	gtk_widget_set_visible(GTK_WIDGET(home), available);
	if (!available && was_home) {
		GtkListBox* navigation = GTK_LIST_BOX(
		    gtk_widget_get_parent(self->home_row)
		);
		gtk_list_box_select_row(
		    navigation,
		    gtk_list_box_get_row_at_index(navigation, 1)
		);
	}
}
static void sign_window_init(SignWindow* self) {
	const GActionEntry actions[] = {
	    {.name = "validate-file",
	     .activate = validate_file,
	     .parameter_type = "s"}
	};
	g_action_map_add_action_entries(
	    G_ACTION_MAP(self),
	    actions,
	    G_N_ELEMENTS(actions),
	    self
	);
	gtk_window_set_title(GTK_WINDOW(self), "CRSign");
	gtk_window_set_default_size(GTK_WINDOW(self), 900, 600);
	gtk_widget_set_size_request(GTK_WIDGET(self), 320, 360);
	self->split_view = ADW_OVERLAY_SPLIT_VIEW(adw_overlay_split_view_new());
	adw_overlay_split_view_set_min_sidebar_width(self->split_view, 180);
	adw_overlay_split_view_set_max_sidebar_width(self->split_view, 220);
	adw_overlay_split_view_set_sidebar_width_fraction(self->split_view, 0.2);
	/* Construct content first: initial sidebar selection updates the page
	 * title. */
	adw_overlay_split_view_set_content(self->split_view, create_content(self));
	adw_overlay_split_view_set_sidebar(self->split_view, create_sidebar(self));
	g_signal_connect_object(
	    self->home,
	    "history-changed",
	    G_CALLBACK(home_history_changed),
	    self,
	    0
	);
	home_history_changed(self->home, self);
	adw_application_window_set_content(
	    ADW_APPLICATION_WINDOW(self),
	    GTK_WIDGET(self->split_view)
	);

	AdwBreakpoint* breakpoint = adw_breakpoint_new(
	    adw_breakpoint_condition_parse("max-width: 640sp")
	);
	GValue collapsed = G_VALUE_INIT;
	g_value_init(&collapsed, G_TYPE_BOOLEAN);
	g_value_set_boolean(&collapsed, TRUE);
	adw_breakpoint_add_setter(
	    breakpoint,
	    G_OBJECT(self->split_view),
	    "collapsed",
	    &collapsed
	);
	g_value_unset(&collapsed);
	adw_application_window_add_breakpoint(
	    ADW_APPLICATION_WINDOW(self),
	    breakpoint
	);
	sign_devices_refresh(
	    SIGN_DEVICES(gtk_stack_get_child_by_name(self->pages, "devices"))
	);
}

static void sign_window_class_init(SignWindowClass* klass) {
	(void)klass;
}

GtkWidget* sign_window_new(GtkApplication* application) {
	return g_object_new(SIGN_TYPE_WINDOW, "application", application, NULL);
}
