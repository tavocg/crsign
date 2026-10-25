/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_GUI_DIALOG_H
#define SIGN_GUI_DIALOG_H
#include <adwaita.h>

/* Read-only detail dialogs dismiss on the dimmed background, without passing
 * that click through to the page beneath. Dialogs that fill the window retain
 * their normal close button and Escape action. */
static inline void dialog_background_pressed(
    GtkGestureClick* gesture,
    int              presses,
    double           x,
    double           y,
    gpointer         data
) {
	(void)presses;
	GtkWidget* root = data;
	AdwDialog* dialog = adw_application_window_get_visible_dialog(
	    ADW_APPLICATION_WINDOW(root)
	);
	if (!dialog || !adw_dialog_get_can_close(dialog)) {
		return;
	}
	graphene_rect_t  bounds;
	graphene_point_t point = GRAPHENE_POINT_INIT(x, y);
	GtkWidget*       child = adw_dialog_get_child(dialog);
	if (child && gtk_widget_compute_bounds(child, root, &bounds) &&
	    !graphene_rect_contains_point(&bounds, &point)) {
		if (gtk_event_controller_get_current_event(
		        GTK_EVENT_CONTROLLER(gesture)
		    )) {
			gtk_gesture_set_state(
			    GTK_GESTURE(gesture),
			    GTK_EVENT_SEQUENCE_CLAIMED
			);
		}
		adw_dialog_close(dialog);
	}
}
static inline void sign_dialog_present(AdwDialog* dialog, GtkWidget* parent) {
	GtkRoot* root = gtk_widget_get_root(parent);
	if (ADW_IS_APPLICATION_WINDOW(root) &&
	    !g_object_get_data(G_OBJECT(root), "dialog-dismiss")) {
		GtkGesture* click = gtk_gesture_click_new();
		gtk_event_controller_set_name(
		    GTK_EVENT_CONTROLLER(click),
		    "dialog-dismiss"
		);
		gtk_gesture_single_set_button(
		    GTK_GESTURE_SINGLE(click),
		    GDK_BUTTON_PRIMARY
		);
		gtk_event_controller_set_propagation_phase(
		    GTK_EVENT_CONTROLLER(click),
		    GTK_PHASE_CAPTURE
		);
		/* Dialogs limit event propagation to their modal subtree. */
		gtk_event_controller_set_propagation_limit(
		    GTK_EVENT_CONTROLLER(click),
		    GTK_LIMIT_NONE
		);
		g_signal_connect(
		    click,
		    "pressed",
		    G_CALLBACK(dialog_background_pressed),
		    root
		);
		gtk_widget_add_controller(
		    GTK_WIDGET(root),
		    GTK_EVENT_CONTROLLER(click)
		);
		g_object_set_data(G_OBJECT(root), "dialog-dismiss", GINT_TO_POINTER(1));
	}
	adw_dialog_present(dialog, parent);
}
#endif
