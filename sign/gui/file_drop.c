/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/gui/file_drop.h>
#include <sign/gui/i18n.h>
#include <stdbool.h>
typedef struct {
	GtkWidget*            widget;
	sign_files_received   received;
	sign_drop_cancellable cancellable;
	sign_drop_state       state;
	gpointer              data;
} drop_binding;
typedef struct {
	drop_binding* binding;
	GdkDrop*      drop;
	GInputStream* stream;
	GCancellable* cancellable;
} drop_request;
static bool deliver(drop_binding* binding, const GValue* value) {
	if (!gtk_widget_get_root(binding->widget) || !value ||
	    !G_VALUE_HOLDS(value, GDK_TYPE_FILE_LIST)) {
		return false;
	}
	GdkFileList* list = g_value_get_boxed(value);
	GSList*      files = list ? gdk_file_list_get_files(list) : NULL;
	if (!files) {
		return false;
	}
	GListStore* model = g_list_store_new(G_TYPE_FILE);
	for (GSList* node = files; node; node = node->next) {
		g_list_store_append(model, node->data);
	}
	binding->received(binding->widget, G_LIST_MODEL(model), binding->data);
	g_object_unref(model);
	return true;
}
static void complete(
    drop_request* request,
    const GValue* value,
    GError*       error
) {
	drop_binding* binding = request->binding;
	bool accepted = !g_cancellable_is_cancelled(request->cancellable) &&
	                deliver(binding, value);
	if (!accepted && !error && gtk_widget_get_root(binding->widget)) {
		error = g_error_new_literal(
		    G_IO_ERROR,
		    G_IO_ERROR_INVALID_DATA,
		    _("No files were received from the file manager")
		);
	}
	/* Reading references never transfers ownership of the originals. */
	gdk_drop_finish(request->drop, accepted ? GDK_ACTION_COPY : 0);
	if (binding->state) {
		binding->state(binding->widget, false, error, binding->data);
	}
	g_clear_error(&error);
	if (request->stream) {
		g_object_unref(request->stream);
	}
	g_object_unref(request->drop);
	g_object_unref(request->cancellable);
	g_object_unref(binding->widget);
	g_free(request);
}
static void deserialized(GObject* source, GAsyncResult* result, gpointer data) {
	(void)source;
	GValue  value = G_VALUE_INIT;
	GError* error = NULL;
	bool    ok = gdk_content_deserialize_finish(result, &value, &error);
	complete(data, ok ? &value : NULL, error);
	if (G_VALUE_TYPE(&value)) {
		g_value_unset(&value);
	}
}
static void received(GObject* source, GAsyncResult* result, gpointer data) {
	drop_request* request = data;
	GError*       error = NULL;
	const char*   mime = NULL;
	request->stream = gdk_drop_read_finish(
	    GDK_DROP(source),
	    result,
	    &mime,
	    &error
	);
	if (!request->stream) {
		complete(request, NULL, error);
		return;
	}
	gdk_content_deserialize_async(
	    request->stream,
	    mime,
	    GDK_TYPE_FILE_LIST,
	    G_PRIORITY_DEFAULT,
	    request->cancellable,
	    deserialized,
	    request
	);
}
static gboolean accept_uri(
    GtkDropTargetAsync* target,
    GdkDrop*            drop,
    gpointer            data
) {
	(void)target;
	drop_binding* binding = data;
	return gtk_widget_get_root(binding->widget) &&
	       gdk_content_formats_contain_mime_type(
	           gdk_drop_get_formats(drop),
	           "text/uri-list"
	       );
}
static GdkDragAction motion(
    GtkDropTargetAsync* target,
    GdkDrop*            drop,
    double              x,
    double              y,
    gpointer            data
) {
	(void)target;
	(void)drop;
	(void)x;
	(void)y;
	(void)data;
	/* Negotiate our preference even if Wayland initially reports MOVE. */
	return GDK_ACTION_COPY;
}
static gboolean dropped_uri(
    GtkDropTargetAsync* target,
    GdkDrop*            drop,
    double              x,
    double              y,
    gpointer            data
) {
	(void)target;
	(void)x;
	(void)y;
	drop_binding* binding = data;
	GCancellable* cancellable = binding->cancellable(
	    binding->widget,
	    binding->data
	);
	if (!cancellable || g_cancellable_is_cancelled(cancellable)) {
		return FALSE;
	}
	drop_request* request = g_new0(drop_request, 1);
	request->binding = binding;
	request->drop = g_object_ref(drop);
	request->cancellable = g_object_ref(cancellable);
	g_object_ref(binding->widget);
	if (binding->state) {
		binding->state(binding->widget, true, NULL, binding->data);
	}
	static const char* mime[] = {"text/uri-list", NULL};
	gdk_drop_read_async(
	    drop,
	    mime,
	    G_PRIORITY_DEFAULT,
	    cancellable,
	    received,
	    request
	);
	return TRUE;
}
static gboolean accept_typed(
    GtkDropTarget* target,
    GdkDrop*       drop,
    gpointer       data
) {
	(void)data;
	return !gdk_content_formats_contain_mime_type(
	           gdk_drop_get_formats(drop),
	           "text/uri-list"
	       ) &&
	       (gdk_drop_get_actions(drop) & GDK_ACTION_COPY) &&
	       gdk_content_formats_match_gtype(
	           gtk_drop_target_get_formats(target),
	           gdk_drop_get_formats(drop)
	       ) != G_TYPE_INVALID;
}
static gboolean dropped_typed(
    GtkDropTarget* target,
    const GValue*  value,
    double         x,
    double         y,
    gpointer       data
) {
	(void)target;
	(void)x;
	(void)y;
	return deliver(data, value);
}
void sign_file_drop_attach(
    GtkWidget*            widget,
    sign_files_received   files,
    sign_drop_cancellable cancellable,
    sign_drop_state       state,
    gpointer              data
) {
	GdkDisplay* display = gtk_widget_get_display(widget);
	if (!g_object_get_data(G_OBJECT(display), "crsign-file-drop-style")) {
		GtkCssProvider* provider = gtk_css_provider_new();
		gtk_css_provider_load_from_resource(
		    provider,
		    "/org/crsign/Sign/style.css"
		);
		gtk_style_context_add_provider_for_display(
		    display,
		    GTK_STYLE_PROVIDER(provider),
		    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
		);
		g_object_set_data_full(
		    G_OBJECT(display),
		    "crsign-file-drop-style",
		    provider,
		    g_object_unref
		);
	}

	drop_binding* binding = g_new0(drop_binding, 1);
	*binding = (drop_binding){widget, files, cancellable, state, data};
	g_object_set_data_full(
	    G_OBJECT(widget),
	    "crsign-file-drop",
	    binding,
	    g_free
	);
	GtkDropTarget* typed = gtk_drop_target_new(
	    GDK_TYPE_FILE_LIST,
	    GDK_ACTION_COPY
	);
	g_signal_connect(typed, "accept", G_CALLBACK(accept_typed), binding);
	g_signal_connect(typed, "drop", G_CALLBACK(dropped_typed), binding);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(typed));
	const char*         mime[] = {"text/uri-list"};
	GtkDropTargetAsync* async = gtk_drop_target_async_new(
	    gdk_content_formats_new(mime, 1),
	    GDK_ACTION_COPY
	);
	gtk_event_controller_set_propagation_phase(
	    GTK_EVENT_CONTROLLER(async),
	    GTK_PHASE_CAPTURE
	);
	g_signal_connect(async, "accept", G_CALLBACK(accept_uri), binding);
	g_signal_connect(async, "drag-enter", G_CALLBACK(motion), binding);
	g_signal_connect(async, "drag-motion", G_CALLBACK(motion), binding);
	g_signal_connect(async, "drop", G_CALLBACK(dropped_uri), binding);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(async));
}
