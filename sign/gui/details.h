/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_GUI_DETAILS_H
#define SIGN_GUI_DETAILS_H
#include <adwaita.h>

/* Plain properties shared by device, slot and validation detail views. Only
 * navigable rows and expanders receive a card background. */
static inline GtkWidget* sign_detail_field(
    GtkWidget*  parent,
    const char* title,
    const char* value
) {
	GtkWidget* field = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_widget_set_margin_start(field, 14);
	gtk_widget_set_margin_end(field, 14);
	gtk_widget_set_margin_top(field, 10);
	gtk_widget_set_margin_bottom(field, 10);
	GtkWidget* labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_set_hexpand(labels, TRUE);
	GtkWidget* name = gtk_label_new(title);
	gtk_label_set_xalign(GTK_LABEL(name), 0);
	gtk_label_set_wrap(GTK_LABEL(name), TRUE);
	gtk_label_set_selectable(GTK_LABEL(name), TRUE);
	char*      safe = g_utf8_make_valid(value ? value : "", -1);
	GtkWidget* text = gtk_label_new(safe);
	g_free(safe);
	gtk_label_set_xalign(GTK_LABEL(text), 0);
	gtk_label_set_wrap(GTK_LABEL(text), TRUE);
	gtk_label_set_wrap_mode(GTK_LABEL(text), PANGO_WRAP_WORD_CHAR);
	gtk_label_set_width_chars(GTK_LABEL(text), 1);
	gtk_label_set_selectable(GTK_LABEL(text), TRUE);
	gtk_widget_add_css_class(text, "dim-label");
	gtk_widget_add_css_class(text, "caption");
	gtk_box_append(GTK_BOX(labels), name);
	gtk_box_append(GTK_BOX(labels), text);
	gtk_box_append(GTK_BOX(field), labels);
	if (ADW_IS_EXPANDER_ROW(parent)) {
		adw_expander_row_add_row(ADW_EXPANDER_ROW(parent), field);
	} else if (GTK_IS_LIST_BOX(parent)) {
		gtk_list_box_append(GTK_LIST_BOX(parent), field);
	} else {
		gtk_box_append(GTK_BOX(parent), field);
	}
	GtkWidget* wrapper = gtk_widget_get_parent(field);
	if (GTK_IS_LIST_BOX_ROW(wrapper)) {
		gtk_widget_add_css_class(wrapper, "plain-detail");
		gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(wrapper), FALSE);
		gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(wrapper), FALSE);
	}
	return field;
}

typedef struct {
	AdwNavigationSplitView* split;
	AdwNavigationPage*      page;
} sign_details;

static inline GtkWidget* sign_details_new(
    sign_details* details,
    GtkWidget*    sidebar,
    GtkWidget*    content,
    const char*   title,
    const char*   name
) {
	details->split = ADW_NAVIGATION_SPLIT_VIEW(adw_navigation_split_view_new());
	gtk_widget_set_name(GTK_WIDGET(details->split), name);
	gtk_widget_set_vexpand(GTK_WIDGET(details->split), TRUE);
	AdwNavigationPage* list = adw_navigation_page_new(sidebar, title);
	adw_navigation_split_view_set_sidebar(details->split, list);
	details->page = adw_navigation_page_new(content, title);
	adw_navigation_split_view_set_content(details->split, details->page);
	adw_navigation_split_view_set_collapsed(details->split, TRUE);
	adw_navigation_split_view_set_show_content(details->split, FALSE);
	return GTK_WIDGET(details->split);
}
#endif
