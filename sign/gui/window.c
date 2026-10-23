/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/gui/about.h>
#include <sign/gui/i18n.h>
#include <sign/gui/settings.h>
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
    {"settings", N_("Settings"), "emblem-system-symbolic"},
    {"about", N_("About"), "help-about-symbolic"},
};

struct _SignWindow {
	AdwApplicationWindow parent_instance;
	AdwOverlaySplitView* split_view;
	GtkStack*            pages;
	GtkLabel*            title;
};

G_DEFINE_TYPE(SignWindow, sign_window, ADW_TYPE_APPLICATION_WINDOW)

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
	if (!strcmp(sections[index].id, "verify")) {
		/* Apply valid pending preferences before validation reads its trust
		 * configuration on the worker thread. Invalid drafts remain unsaved. */
		sign_settings_flush(
		    SIGN_SETTINGS(gtk_stack_get_child_by_name(self->pages, "settings"))
		);
	}
	gtk_stack_set_visible_child_name(self->pages, sections[index].id);
	gtk_label_set_label(self->title, _(sections[index].title));
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
		GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
		gtk_widget_set_margin_start(box, 12);
		gtk_widget_set_margin_end(box, 12);
		gtk_widget_set_margin_top(box, 10);
		gtk_widget_set_margin_bottom(box, 10);
		gtk_box_append(
		    GTK_BOX(box),
		    gtk_image_new_from_icon_name(sections[i].icon)
		);
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
	    gtk_list_box_get_row_at_index(GTK_LIST_BOX(list), 0)
	);
	return toolbar;
}

static GtkWidget* create_content(SignWindow* self) {
	GtkWidget* toolbar = adw_toolbar_view_new();
	GtkWidget* header = adw_header_bar_new();
	self->title = GTK_LABEL(gtk_label_new(_("Home")));
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
	adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);

	self->pages = GTK_STACK(gtk_stack_new());
	gtk_stack_set_transition_type(
	    self->pages,
	    GTK_STACK_TRANSITION_TYPE_CROSSFADE
	);
	gtk_stack_set_transition_duration(self->pages, 150);
	for (guint i = 0; i < G_N_ELEMENTS(sections); ++i) {
		if (!strcmp(sections[i].id, "verify")) {
			gtk_stack_add_titled(
			    self->pages,
			    sign_validation_new(),
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
	adw_toolbar_view_set_content(
	    ADW_TOOLBAR_VIEW(toolbar),
	    GTK_WIDGET(self->pages)
	);
	return toolbar;
}

static void sign_window_init(SignWindow* self) {
	gtk_window_set_title(GTK_WINDOW(self), "CRSign");
	gtk_window_set_default_size(GTK_WINDOW(self), 900, 600);
	gtk_widget_set_size_request(GTK_WIDGET(self), 320, 360);
	self->split_view = ADW_OVERLAY_SPLIT_VIEW(adw_overlay_split_view_new());
	adw_overlay_split_view_set_min_sidebar_width(self->split_view, 200);
	adw_overlay_split_view_set_max_sidebar_width(self->split_view, 240);
	adw_overlay_split_view_set_sidebar_width_fraction(self->split_view, 0.25);
	/* Construct content first: initial sidebar selection updates the page
	 * title. */
	adw_overlay_split_view_set_content(self->split_view, create_content(self));
	adw_overlay_split_view_set_sidebar(self->split_view, create_sidebar(self));
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
}

static void sign_window_class_init(SignWindowClass* klass) {
	(void)klass;
}

GtkWidget* sign_window_new(GtkApplication* application) {
	return g_object_new(SIGN_TYPE_WINDOW, "application", application, NULL);
}
