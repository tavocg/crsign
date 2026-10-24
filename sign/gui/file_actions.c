/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/gui/error.h>
#include <sign/gui/file_actions.h>
#include <sign/gui/i18n.h>
typedef struct {
	GtkWidget*            widget;
	GFile*                file;
	bool                  allow_favorite, allow_validation;
	sign_favorite_changed favorite;
	gpointer              data;
	GtkWidget*            popover;
	GSimpleAction*        favorite_action;
} file_actions;
typedef struct {
	GtkWindow* parent;
	GFile*     file;
	bool       fallback;
} location_request;
static void location_free(location_request* request) {
	if (request->parent) {
		g_object_unref(request->parent);
	}
	g_object_unref(request->file);
	g_free(request);
}
static void location_finished(
    GObject*      source,
    GAsyncResult* result,
    gpointer      data
) {
	location_request* request = data;
	GError*           error = NULL;
	bool              ok = request->fallback
	                           ? gtk_file_launcher_launch_finish(
                        GTK_FILE_LAUNCHER(source),
                        result,
                        &error
                    )
	                           : gtk_file_launcher_open_containing_folder_finish(
                        GTK_FILE_LAUNCHER(source),
                        result,
                        &error
                    );
	if (!ok && !request->fallback &&
	    !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
	    !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) {
		g_clear_error(&error);
		GFile* parent = g_file_get_parent(request->file);
		if (parent) {
			request->fallback = true;
			GtkFileLauncher* launcher = gtk_file_launcher_new(parent);
			g_object_unref(parent);
			gtk_file_launcher_launch(
			    launcher,
			    request->parent,
			    NULL,
			    location_finished,
			    request
			);
			g_object_unref(launcher);
			return;
		}
	}
	if (error && request->parent &&
	    gtk_widget_get_visible(GTK_WIDGET(request->parent)) &&
	    !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
	    !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) {
		gui_error_dialog(
		    request->parent,
		    _("Could not open file location"),
		    _("The containing folder could not be opened. Check that it still "
		      "exists and that you have permission to access it.")
		);
	}
	g_clear_error(&error);
	location_free(request);
}
typedef struct {
	GtkWindow* parent;
	GFile*     file;
} open_request;
static void open_request_free(open_request* request) {
	if (request->parent) {
		g_object_unref(request->parent);
	}
	g_object_unref(request->file);
	g_free(request);
}
static void open_finished(
    GObject*      source,
    GAsyncResult* result,
    gpointer      data
) {
	open_request* request = data;
	GError*       error = NULL;
	bool          ok = gtk_file_launcher_launch_finish(
        GTK_FILE_LAUNCHER(source),
        result,
        &error
    );
	if (!ok && request->parent &&
	    gtk_widget_get_visible(GTK_WIDGET(request->parent)) &&
	    !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
	    !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) {
		gui_error_dialog(
		    request->parent,
		    _("Could not open file"),
		    _("The file could not be opened with the default application. "
		      "Check that it still exists and that an application is "
		      "available for this file type.")
		);
	}
	g_clear_error(&error);
	open_request_free(request);
}
static void open_file(
    GSimpleAction* action,
    GVariant*      parameter,
    gpointer       data
) {
	(void)action;
	(void)parameter;
	file_actions* binding = data;
	open_request* request = g_new0(open_request, 1);
	request->file = g_object_ref(binding->file);
	GtkRoot* root = gtk_widget_get_root(binding->widget);
	if (GTK_IS_WINDOW(root)) {
		request->parent = g_object_ref(GTK_WINDOW(root));
	}
	GtkFileLauncher* launcher = gtk_file_launcher_new(request->file);
	gtk_file_launcher_launch(
	    launcher,
	    request->parent,
	    NULL,
	    open_finished,
	    request
	);
	g_object_unref(launcher);
}
static void location(
    GSimpleAction* action,
    GVariant*      parameter,
    gpointer       data
) {
	(void)action;
	(void)parameter;
	file_actions*     binding = data;
	location_request* request = g_new0(location_request, 1);
	request->file = g_object_ref(binding->file);
	GtkRoot* root = gtk_widget_get_root(binding->widget);
	if (GTK_IS_WINDOW(root)) {
		request->parent = g_object_ref(GTK_WINDOW(root));
	}
	GtkFileLauncher* launcher = gtk_file_launcher_new(request->file);
	gtk_file_launcher_open_containing_folder(
	    launcher,
	    request->parent,
	    NULL,
	    location_finished,
	    request
	);
	g_object_unref(launcher);
}
static void validate_file(
    GSimpleAction* action,
    GVariant*      parameter,
    gpointer       data
) {
	(void)action;
	(void)parameter;
	file_actions* binding = data;
	char*         uri = g_file_get_uri(binding->file);
	gtk_widget_activate_action(binding->widget, "win.validate-file", "s", uri);
	g_free(uri);
}
static void favorite(
    GSimpleAction* action,
    GVariant*      parameter,
    gpointer       data
) {
	(void)parameter;
	file_actions* binding = data;
	GVariant*     state = g_action_get_state(G_ACTION(action));
	bool          active = !g_variant_get_boolean(state);
	g_variant_unref(state);
	g_simple_action_set_state(action, g_variant_new_boolean(active));
	if (binding->favorite) {
		binding->favorite(binding->file, active, binding->data);
	}
}
static void clear_popover(file_actions* binding) {
	if (!binding->popover) {
		return;
	}
	GtkWidget* popover = binding->popover;
	binding->popover = NULL;
	g_signal_handlers_disconnect_by_data(popover, binding);
	gtk_widget_unparent(popover);
}
static void unrealize(GtkWidget* widget, gpointer data) {
	(void)widget;
	clear_popover(data);
}
static void pressed(
    GtkGestureClick* gesture,
    int              count,
    double           x,
    double           y,
    gpointer         data
) {
	(void)count;
	file_actions* binding = data;
	clear_popover(binding);
	GMenu* model = g_menu_new();
	g_menu_append(model, _("Open file location"), "document.location");
	if (binding->allow_favorite) {
		g_menu_append(model, _("Favorite"), "document.favorite");
	}
	if (binding->allow_validation) {
		g_menu_append(model, _("Validate"), "document.validate");
	}
	GtkWidget* popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(model));
	g_object_unref(model);
	binding->popover = popover;
	gtk_widget_set_parent(popover, binding->widget);
	GdkRectangle rectangle = {(int)x, (int)y, 1, 1};
	gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rectangle);
	/* Menu activation closes the popover before dispatching its action.
	 * Keep it parented until the next opening or unrealize so action lookup
	 * can still reach the document action group. */
	gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
	gtk_popover_popup(GTK_POPOVER(popover));
}
static void binding_free(gpointer data) {
	file_actions* binding = data;
	g_object_unref(binding->file);
	g_object_unref(binding->favorite_action);
	g_free(binding);
}
void sign_file_actions_attach(
    GtkWidget*            widget,
    GFile*                file,
    bool                  allow_favorite,
    bool                  active,
    sign_favorite_changed callback,
    gpointer              data
) {
	file_actions* binding = g_object_get_data(
	    G_OBJECT(widget),
	    "crsign-file-actions"
	);
	if (!binding) {
		binding = g_new0(file_actions, 1);
		binding->widget = widget;
		GSimpleActionGroup* group = g_simple_action_group_new();
		GSimpleAction* location_action = g_simple_action_new("location", NULL);
		g_signal_connect(
		    location_action,
		    "activate",
		    G_CALLBACK(location),
		    binding
		);
		g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(location_action));
		g_object_unref(location_action);
		GSimpleAction* open_action = g_simple_action_new("open", NULL);
		g_signal_connect(
		    open_action,
		    "activate",
		    G_CALLBACK(open_file),
		    binding
		);
		g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(open_action));
		g_object_unref(open_action);
		GSimpleAction* validate = g_simple_action_new("validate", NULL);
		g_signal_connect(
		    validate,
		    "activate",
		    G_CALLBACK(validate_file),
		    binding
		);
		g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(validate));
		g_object_unref(validate);
		binding->favorite_action = g_simple_action_new_stateful(
		    "favorite",
		    NULL,
		    g_variant_new_boolean(active)
		);
		g_signal_connect(
		    binding->favorite_action,
		    "activate",
		    G_CALLBACK(favorite),
		    binding
		);
		g_action_map_add_action(
		    G_ACTION_MAP(group),
		    G_ACTION(binding->favorite_action)
		);
		gtk_widget_insert_action_group(
		    widget,
		    "document",
		    G_ACTION_GROUP(group)
		);
		g_object_unref(group);
		GtkGesture* click = gtk_gesture_click_new();
		gtk_gesture_single_set_button(
		    GTK_GESTURE_SINGLE(click),
		    GDK_BUTTON_SECONDARY
		);
		g_signal_connect(click, "pressed", G_CALLBACK(pressed), binding);
		gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(click));
		g_signal_connect(widget, "unrealize", G_CALLBACK(unrealize), binding);
		g_object_set_data_full(
		    G_OBJECT(widget),
		    "crsign-file-actions",
		    binding,
		    binding_free
		);
	}
	if (binding->file) {
		g_object_unref(binding->file);
	}
	binding->file = g_object_ref(file);
	binding->allow_favorite = allow_favorite;
	binding->favorite = callback;
	binding->data = data;
	g_simple_action_set_enabled(binding->favorite_action, allow_favorite);
	g_simple_action_set_state(
	    binding->favorite_action,
	    g_variant_new_boolean(active)
	);
}
void sign_file_allow_validation(GtkWidget* widget) {
	file_actions* binding = g_object_get_data(
	    G_OBJECT(widget),
	    "crsign-file-actions"
	);
	g_return_if_fail(binding != NULL);
	binding->allow_validation = true;
}
void sign_file_drag_attach(GtkWidget* widget, GFile* file) {
	GtkDragSource* source = g_object_get_data(
	    G_OBJECT(widget),
	    "crsign-file-drag"
	);
	if (!source) {
		source = gtk_drag_source_new();
		gtk_drag_source_set_actions(source, GDK_ACTION_COPY);
		gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(source));
		g_object_set_data(G_OBJECT(widget), "crsign-file-drag", source);
	}
	GValue value = G_VALUE_INIT;
	g_value_init(&value, GDK_TYPE_FILE_LIST);
	g_value_take_boxed(&value, gdk_file_list_new_from_array(&file, 1));
	GdkContentProvider* content = gdk_content_provider_new_for_value(&value);
	gtk_drag_source_set_content(source, content);
	g_object_unref(content);
	g_value_unset(&value);
}
GtkWidget* sign_file_location_button(void) {
	GtkWidget* button = gtk_button_new_from_icon_name("folder-open-symbolic");
	gtk_widget_add_css_class(button, "flat");
	gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
	gtk_widget_set_name(button, "document-location");
	gtk_widget_set_tooltip_text(button, _("Open file location"));
	gtk_actionable_set_action_name(GTK_ACTIONABLE(button), "document.location");
	return button;
}
GtkWidget* sign_file_favorite_button(bool active) {
	GtkWidget* button = gtk_toggle_button_new();
	gtk_button_set_icon_name(
	    GTK_BUTTON(button),
	    active ? "starred-symbolic" : "non-starred-symbolic"
	);
	gtk_widget_add_css_class(button, "flat");
	gtk_widget_set_name(button, "document-favorite");
	gtk_widget_add_css_class(button, "document-favorite");
	gtk_widget_set_size_request(button, 34, 34);
	gtk_widget_set_tooltip_text(button, _("Favorite"));
	gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
	gtk_actionable_set_action_name(GTK_ACTIONABLE(button), "document.favorite");
	return button;
}
