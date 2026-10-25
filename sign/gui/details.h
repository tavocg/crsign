/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_GUI_DETAILS_H
#define SIGN_GUI_DETAILS_H
#include <adwaita.h>
#include <sign/gui/i18n.h>

typedef struct {
	AdwNavigationSplitView* split;
	AdwNavigationPage*      page;
} sign_details;

static inline void details_back(GtkButton* button, gpointer data) {
	(void)button;
	adw_navigation_split_view_set_show_content(data, FALSE);
}

static inline GtkWidget* sign_details_new(
    sign_details* details,
    GtkWidget*    sidebar,
    GtkWidget*    content,
    const char*   title,
    const char*   name
) {
	details->split = ADW_NAVIGATION_SPLIT_VIEW(adw_navigation_split_view_new());
	gtk_widget_set_name(GTK_WIDGET(details->split), name);
	adw_navigation_split_view_set_min_sidebar_width(details->split, 240);
	adw_navigation_split_view_set_max_sidebar_width(details->split, 320);
	adw_navigation_split_view_set_sidebar_width_fraction(details->split, 0.36);
	AdwNavigationPage* list = adw_navigation_page_new(sidebar, title);
	adw_navigation_split_view_set_sidebar(details->split, list);
	GtkWidget* toolbar = adw_toolbar_view_new();
	GtkWidget* header = adw_header_bar_new();
	adw_header_bar_set_show_start_title_buttons(ADW_HEADER_BAR(header), FALSE);
	adw_header_bar_set_show_end_title_buttons(ADW_HEADER_BAR(header), FALSE);
	adw_header_bar_set_show_back_button(ADW_HEADER_BAR(header), FALSE);
	GtkWidget* back = gtk_button_new_from_icon_name("go-previous-symbolic");
	gtk_widget_set_name(back, "details-back");
	gtk_widget_set_tooltip_text(back, _("Back"));
	gtk_accessible_update_property(
	    GTK_ACCESSIBLE(back),
	    GTK_ACCESSIBLE_PROPERTY_LABEL,
	    _("Back"),
	    -1
	);
	g_signal_connect_object(
	    back,
	    "clicked",
	    G_CALLBACK(details_back),
	    details->split,
	    0
	);
	g_object_bind_property(
	    details->split,
	    "collapsed",
	    back,
	    "visible",
	    G_BINDING_SYNC_CREATE
	);
	adw_header_bar_pack_start(ADW_HEADER_BAR(header), back);
	adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);
	adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), content);
	details->page = adw_navigation_page_new(toolbar, title);
	adw_navigation_split_view_set_content(details->split, details->page);
	adw_navigation_split_view_set_show_content(details->split, FALSE);
	GtkWidget* bin = adw_breakpoint_bin_new();
	gtk_widget_set_size_request(bin, 280, 100);
	gtk_widget_set_vexpand(bin, TRUE);
	adw_breakpoint_bin_set_child(
	    ADW_BREAKPOINT_BIN(bin),
	    GTK_WIDGET(details->split)
	);
	AdwBreakpoint* breakpoint = adw_breakpoint_new(
	    adw_breakpoint_condition_parse("max-width: 700sp")
	);
	GValue collapsed = G_VALUE_INIT;
	g_value_init(&collapsed, G_TYPE_BOOLEAN);
	g_value_set_boolean(&collapsed, TRUE);
	adw_breakpoint_add_setter(
	    breakpoint,
	    G_OBJECT(details->split),
	    "collapsed",
	    &collapsed
	);
	g_value_unset(&collapsed);
	adw_breakpoint_bin_add_breakpoint(ADW_BREAKPOINT_BIN(bin), breakpoint);
	return bin;
}
#endif
