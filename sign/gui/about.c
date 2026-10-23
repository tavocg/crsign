/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <adwaita.h>
#include <sign/gui/about.h>
#include <sign/gui/config.h>
#include <sign/gui/i18n.h>

GtkWidget* sign_about_new(void) {
	GtkWidget* scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(
	    GTK_SCROLLED_WINDOW(scroll),
	    GTK_POLICY_NEVER,
	    GTK_POLICY_AUTOMATIC
	);
	GtkWidget* clamp = adw_clamp_new();
	adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 480);
	GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_valign(content, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_top(content, 32);
	gtk_widget_set_margin_bottom(content, 32);
	gtk_widget_set_margin_start(content, 24);
	gtk_widget_set_margin_end(content, 24);

	GtkWidget* icon = gtk_image_new_from_resource(
	    "/org/crsign/Sign/icon-rounded.svg"
	);
	gtk_image_set_pixel_size(GTK_IMAGE(icon), 128);
	gtk_widget_set_name(icon, "about-icon");
	gtk_accessible_update_property(
	    GTK_ACCESSIBLE(icon),
	    GTK_ACCESSIBLE_PROPERTY_LABEL,
	    "CR Sign",
	    -1
	);
	gtk_box_append(GTK_BOX(content), icon);
	GtkWidget* title = gtk_label_new("CR Sign v" SIGN_GUI_VERSION);
	gtk_widget_set_name(title, "about-version");
	gtk_widget_add_css_class(title, "title-1");
	gtk_label_set_wrap(GTK_LABEL(title), TRUE);
	gtk_box_append(GTK_BOX(content), title);

	GtkWidget* description = gtk_label_new(
	    _("Digital signing and signature validation")
	);
	gtk_label_set_wrap(GTK_LABEL(description), TRUE);
	gtk_label_set_justify(GTK_LABEL(description), GTK_JUSTIFY_CENTER);
	gtk_widget_add_css_class(description, "dim-label");
	gtk_box_append(GTK_BOX(content), description);
	GtkWidget* copyright = gtk_label_new("Copyright (C) 2026 CR Sign");
	gtk_label_set_wrap(GTK_LABEL(copyright), TRUE);
	gtk_label_set_justify(GTK_LABEL(copyright), GTK_JUSTIFY_CENTER);
	gtk_label_set_selectable(GTK_LABEL(copyright), TRUE);
	gtk_box_append(GTK_BOX(content), copyright);

	GtkWidget* links = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_halign(links, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_top(links, 6);
	gtk_box_append(
	    GTK_BOX(links),
	    gtk_link_button_new_with_label(
	        "https://github.com/tavocg/crsign/blob/main/LICENSE",
	        _("License (GPL-3.0-or-later)")
	    )
	);
	gtk_box_append(
	    GTK_BOX(links),
	    gtk_link_button_new_with_label(
	        "https://github.com/tavocg/crsign",
	        _("Source code")
	    )
	);
	gtk_box_append(
	    GTK_BOX(links),
	    gtk_link_button_new_with_label(
	        "https://github.com/tavocg/crsign/issues",
	        _("Report an issue")
	    )
	);
	gtk_box_append(GTK_BOX(content), links);
	adw_clamp_set_child(ADW_CLAMP(clamp), content);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), clamp);
	return scroll;
}
