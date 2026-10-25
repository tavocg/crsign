/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <glib/gstdio.h>
#include <math.h>
#include <sign/appearance.h>
#include <sign/config.h>
#include <sign/file_state.h>
#include <sign/gui/device_access.h>
#include <sign/gui/error.h>
#include <sign/gui/file_actions.h>
#include <sign/gui/file_drop.h>
#include <sign/gui/i18n.h>
#include <sign/gui/pdf_reader.h>
#include <sign/gui/pdf_view.h>
#include <sign/gui/signing.h>
#include <sign/pin.h>
#include <sign/preferences.h>
#include <sign/sign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	gint               references;
	char *             path, *output, *image, *profile_image;
	bool               image_set, checked, information, is_pdf;
	bool               output_exists, output_duplicate, output_input_collision;
	sign_pdf_document* pdf;
	libssig_pdf_appearance appearance;
	GtkWidget *            row, *warning, *output_button;
} signing_item;
struct _SignSigning {
	GtkBox       parent_instance;
	GPtrArray *  items, *profile_ids;
	GtkDropDown* profiles;
	GtkStack*    modes;
	GtkListBox*  list;
	GtkWidget *  canvas, *image_button, *image_menu, *default_button,
	    *information;
	SignPdfReader* reader;
	GtkWidget *continue_button, *sign_button, *warning, *add, *back, *spinner;
	GtkWidget *output_options, *conflict_dialog;
	GtkLabel * notice, *filename, *page_count, *total;
	GtkWidget* clear;
	GtkSpinButton*   page;
	signing_item*    current;
	cairo_surface_t *surface, *stamp;
	GCancellable *   lifetime, *preview;
	GtkWindow*       pin_window;
	unsigned int     revision, page_number;
	double           page_width, page_height, ratio;
	bool             updating, loading, busy, closing;
};
G_DEFINE_TYPE(SignSigning, sign_signing, GTK_TYPE_BOX)
static guint documents_signed, signing_completed;

static signing_item* item_ref(signing_item* item) {
	g_atomic_int_inc(&item->references);
	return item;
}
static void item_unref(gpointer data) {
	signing_item* item = data;
	if (!g_atomic_int_dec_and_test(&item->references)) {
		return;
	}
	g_free(item->path);
	g_free(item->output);
	g_free(item->image);
	g_free(item->profile_image);
	sign_pdf_unref(item->pdf);
	g_free(item);
}
static void message(SignSigning* self, const char* text) {
	gtk_label_set_text(self->notice, text ? text : "");
	gtk_widget_set_visible(GTK_WIDGET(self->notice), text && *text);
}
static GtkWindow* parent_window(SignSigning* self);
static bool       refresh_output_warnings(SignSigning* self);
static void       update_warning_widgets(SignSigning* self);
static void       start_run(SignSigning* self, bool overwrite);
static void       change_output_clicked(GtkButton* button, gpointer data);
static void       set_item_output(
          SignSigning*  self,
          signing_item* item,
          const char*   output
      );
static void report_error(
    SignSigning*      self,
    const GError*     error,
    gui_error_context context
) {
	char* text = gui_error_message(error, context);
	message(self, text);
	g_free(text);
}
static const char* profile(SignSigning* self) {
	guint index = gtk_drop_down_get_selected(self->profiles);
	return index < self->profile_ids->len
	           ? g_ptr_array_index(self->profile_ids, index)
	           : NULL;
}
static void append_warning(GString* text, const char* warning) {
	if (text->len) {
		g_string_append_c(text, '\n');
	}
	g_string_append(text, warning);
}
static bool file_is_pdf(const char* path) {
	FILE* file = g_fopen(path, "rb");
	if (!file) {
		return false;
	}
	char magic[5];
	bool pdf = fread(magic, 1, sizeof(magic), file) == sizeof(magic) &&
	           !memcmp(magic, "%PDF-", sizeof(magic));
	fclose(file);
	return pdf;
}
static bool item_warning_text(signing_item* item, GString* text) {
	if (item->is_pdf && !item->appearance.page_number) {
		append_warning(text, _("No graphical signature"));
	}
	if (!item->output || !item->output[0]) {
		append_warning(text, _("The output path is unavailable"));
	}
	if (item->output_exists) {
		append_warning(text, _("The output file already exists"));
	}
	if (item->output_duplicate) {
		append_warning(text, _("Another document uses the same output path"));
	}
	if (item->output_input_collision) {
		append_warning(text, _("The output path matches an input file"));
	}
	return text->len > 0;
}
static void update_warning_widgets(SignSigning* self) {
	if (!self->warning) {
		return;
	}
	bool footer = false;
	for (guint n = 0; n < self->items->len; ++n) {
		signing_item* item = g_ptr_array_index(self->items, n);
		GString*      text = g_string_new(NULL);
		bool          visible = item_warning_text(item, text);
		gtk_widget_set_visible(item->warning, visible);
		gtk_widget_set_tooltip_text(item->warning, visible ? text->str : NULL);
		footer |= visible;
		g_string_free(text, TRUE);
	}
	GString* tooltip = g_string_new(NULL);
	bool missing = false, exists = false, duplicate = false, collision = false,
	     unavailable = false;
	for (guint n = 0; n < self->items->len; ++n) {
		signing_item* item = g_ptr_array_index(self->items, n);
		missing |= item->is_pdf && !item->appearance.page_number;
		exists |= item->output_exists;
		duplicate |= item->output_duplicate;
		collision |= item->output_input_collision;
		unavailable |= !item->output || !item->output[0];
	}
	if (missing) {
		append_warning(tooltip, _("Some files have no graphical signature"));
	}
	if (exists) {
		append_warning(tooltip, _("Some output files already exist"));
	}
	if (duplicate) {
		append_warning(tooltip, _("Some documents share an output path"));
	}
	if (collision) {
		append_warning(tooltip, _("Some output paths match an input file"));
	}
	if (unavailable) {
		append_warning(tooltip, _("Some output paths are unavailable"));
	}
	gtk_widget_set_visible(self->warning, footer);
	gtk_widget_set_tooltip_text(
	    self->warning,
	    tooltip->len ? tooltip->str : NULL
	);
	g_string_free(tooltip, TRUE);
}
static bool refresh_output_warnings(SignSigning* self) {
	GHashTable* inputs = g_hash_table_new_full(
	    (GHashFunc)g_file_hash,
	    (GEqualFunc)g_file_equal,
	    g_object_unref,
	    NULL
	);
	GHashTable* outputs = g_hash_table_new_full(
	    (GHashFunc)g_file_hash,
	    (GEqualFunc)g_file_equal,
	    g_object_unref,
	    NULL
	);
	for (guint n = 0; n < self->items->len; ++n) {
		signing_item* item = g_ptr_array_index(self->items, n);
		g_hash_table_add(inputs, g_file_new_for_path(item->path));
	}
	for (guint n = 0; n < self->items->len; ++n) {
		signing_item* item = g_ptr_array_index(self->items, n);
		item->output_exists = false;
		item->output_duplicate = false;
		item->output_input_collision = false;
		if (!item->output || !item->output[0]) {
			continue;
		}
		GFile*   output = g_file_new_for_path(item->output);
		gpointer value = NULL;
		if (g_hash_table_lookup_extended(outputs, output, NULL, &value)) {
			guint count = GPOINTER_TO_UINT(value) + 1;
			g_hash_table_replace(
			    outputs,
			    g_object_ref(output),
			    GUINT_TO_POINTER(count)
			);
			g_object_unref(output);
		} else {
			g_hash_table_insert(outputs, output, GUINT_TO_POINTER(1));
		}
	}
	bool hard = false;
	for (guint n = 0; n < self->items->len; ++n) {
		signing_item* item = g_ptr_array_index(self->items, n);
		if (!item->output || !item->output[0]) {
			hard = true;
			continue;
		}
		GFile* output = g_file_new_for_path(item->output);
		item->output_input_collision = g_hash_table_contains(inputs, output);
		item->output_duplicate = GPOINTER_TO_UINT(
		                             g_hash_table_lookup(outputs, output)
		                         ) > 1;
		item->output_exists = g_file_query_exists(output, NULL);
		hard |= item->output_input_collision || item->output_duplicate;
		g_object_unref(output);
	}
	g_hash_table_unref(inputs);
	g_hash_table_unref(outputs);
	update_warning_widgets(self);
	return hard;
}
static void controls(SignSigning* self) {
	char* total = g_strdup_printf(_("Total: %u"), self->items->len);
	gtk_label_set_text(self->total, total);
	g_free(total);
	gtk_widget_set_sensitive(self->clear, !self->busy);

	update_warning_widgets(self);
	gtk_widget_set_sensitive(
	    self->sign_button,
	    self->items->len && !self->busy && !self->loading
	);
	gtk_widget_set_visible(self->output_options, self->items->len > 0);
	gtk_widget_set_sensitive(
	    self->output_options,
	    self->items->len > 0 && !self->busy
	);
	bool reader = sign_signing_is_reader(self);
	gtk_widget_set_visible(self->add, !reader);
	gtk_widget_set_vexpand(self->add, !self->items->len);
	gtk_widget_set_visible(GTK_WIDGET(self->modes), self->items->len > 0);
	gtk_widget_set_sensitive(self->add, !self->busy);
	gtk_widget_set_sensitive(GTK_WIDGET(self->profiles), !self->busy);
	gtk_widget_set_sensitive(
	    self->list ? GTK_WIDGET(self->list) : GTK_WIDGET(self),
	    !self->busy
	);
	gtk_widget_set_sensitive(
	    self->canvas,
	    !self->busy && !self->loading && self->surface
	);
	gtk_widget_set_sensitive(
	    GTK_WIDGET(self->page),
	    !self->loading && !self->busy && self->surface
	);
	gtk_widget_set_sensitive(
	    self->image_button,
	    !self->loading && !self->busy && self->surface
	);
	gtk_widget_set_visible(
	    self->default_button,
	    self->current && self->current->image_set &&
	        g_strcmp0(self->current->image, self->current->profile_image) != 0
	);
	gtk_widget_set_sensitive(
	    self->default_button,
	    !self->loading && !self->busy && self->surface
	);
	gtk_widget_set_visible(
	    self->image_menu,
	    self->current && self->current->image
	);
	gtk_widget_set_sensitive(
	    self->image_menu,
	    !self->loading && !self->busy && self->surface
	);
	gtk_widget_set_visible(
	    self->information,
	    self->current && self->current->image
	);
	gtk_widget_set_sensitive(
	    self->information,
	    !self->loading && !self->busy && self->surface
	);
	if (!self->loading) {
		self->updating = true;
		gtk_check_button_set_active(
		    GTK_CHECK_BUTTON(self->information),
		    self->current ? self->current->information : true
		);
		self->updating = false;
	}
	gtk_widget_set_sensitive(
	    self->continue_button,
	    !self->loading && !self->busy
	);
	gtk_button_set_label(
	    GTK_BUTTON(self->continue_button),
	    self->current && self->current->appearance.page_number
	        ? _("Continue")
	        : _("Continue without graphical signature")
	);
	gtk_button_set_label(
	    GTK_BUTTON(self->sign_button),
	    self->busy ? _("Signing…") : _("Sign")
	);
	gtk_widget_set_visible(self->spinner, self->loading || self->busy);
	if (self->loading || self->busy) {
		gtk_spinner_start(GTK_SPINNER(self->spinner));
	} else {
		gtk_spinner_stop(GTK_SPINNER(self->spinner));
	}
}
static void redraw(SignSigning* self) {
	sign_pdf_reader_set_stamp(
	    self->reader,
	    self->loading ? NULL : self->stamp,
	    self->current ? &self->current->appearance : NULL
	);
	controls(self);
}
static void reader_page(
    SignPdfReader* reader,
    unsigned int   page,
    gpointer       data
) {
	(void)reader;
	SignSigning* self = data;
	self->page_number = page;
	if (self->current && self->current->pdf) {
		sign_pdf_size(
		    self->current->pdf,
		    page,
		    &self->page_width,
		    &self->page_height
		);
	}
	self->updating = true;
	gtk_spin_button_set_value(self->page, page);
	self->updating = false;
}
bool sign_signing_place(
    SignSigning* self,
    double       x,
    double       y,
    double       width,
    double       height,
    GError**     error
) {
	if (!self->current || !self->surface || !self->stamp || self->loading ||
	    self->busy) {
		g_set_error_literal(
		    error,
		    SIGN_GUI_ERROR,
		    G_IO_ERROR_PENDING,
		    _("Wait for the PDF preview to finish loading")
		);
		return false;
	}
	libssig_pdf_appearance appearance = {.page_number = self->page_number};
	if (!sign_pdf_place(
	        self->page_width,
	        self->page_height,
	        self->ratio,
	        x,
	        y,
	        width,
	        height,
	        &appearance,
	        error
	    )) {
		return false;
	}
	self->current->appearance = appearance;
	message(self, NULL);
	redraw(self);
	return true;
}
static void reader_place(
    SignPdfReader* reader,
    unsigned int   page,
    double         x,
    double         y,
    double         w,
    double         h,
    gpointer       data
) {
	SignSigning* self = data;
	reader_page(reader, page, self);
	GError* error = NULL;
	if (!sign_signing_place(self, x, y, w, h, &error)) {
		report_error(self, error, GUI_ERROR_FILES);
		g_clear_error(&error);
	}
}
void sign_signing_remove_appearance(SignSigning* self) {
	if (self->current && !self->busy) {
		self->current->appearance = (libssig_pdf_appearance){0};
		redraw(self);
	}
}
static void remove_appearance(SignPdfReader* button, gpointer data) {
	(void)button;
	sign_signing_remove_appearance(data);
}

typedef struct {
	signing_item*      item;
	sign_pdf_document* pdf;
	char *             profile, *image, *profile_image;
	bool               image_set, override, information, information_changed;
	unsigned int       page, revision;
	cairo_surface_t *  surface, *stamp;
	double             ratio;
	libssig_pdf_appearance appearance;
} preview_job;
static void preview_free(gpointer data) {
	preview_job* job = data;
	item_unref(job->item);
	sign_pdf_unref(job->pdf);
	g_free(job->profile);
	g_free(job->image);
	g_free(job->profile_image);
	if (job->surface) {
		cairo_surface_destroy(job->surface);
	}
	if (job->stamp) {
		cairo_surface_destroy(job->stamp);
	}
	g_free(job);
}
static void preview_worker(
    GTask*        task,
    gpointer      source,
    gpointer      data,
    GCancellable* cancellable
) {
	(void)source;
	preview_job* job = data;
	GError*      error = NULL;
	if (!job->pdf) {
		GFile* file = g_file_new_for_path(job->item->path);
		job->pdf = sign_pdf_open(file, cancellable, &error);
		g_object_unref(file);
	}
	if (!job->pdf) {
		if (error) {
			g_task_return_error(task, error);
		} else {
			g_task_return_boolean(task, TRUE);
		}
		return;
	}
	job->surface = sign_pdf_render(job->pdf, job->page, cancellable, &error);
	if (!job->surface) {
		g_task_return_error(task, error);
		return;
	}
	sign_args settings = {0};
	char      detail[512];
	if (!config_load(&settings, job->profile, detail, sizeof(detail))) {
		g_task_return_new_error(
		    task,
		    SIGN_GUI_ERROR,
		    G_IO_ERROR_INVALID_DATA,
		    "%s",
		    _("The signing profile could not be loaded. Check the "
		      "configuration in Settings.")
		);
		return;
	}
	job->profile_image = g_strdup(settings.options.appearance_file);
	if (!job->image_set) {
		g_free(job->image);
		job->image = g_strdup(settings.options.appearance_file);
	}
	sign_identity identity = {0};
	char*         svg = NULL;
	if (!job->image || job->information) {
		sign_device_access_writer_lock();
		if (sign_identity_open(&settings.options, &identity, NULL) ==
		    LIBSSIG_OK) {
			svg = appearance_default_svg(identity.certificate);
		}
		sign_identity_close(&identity);
		sign_device_access_writer_unlock();
	}
	if (!svg) {
		svg = appearance_text_svg(_("Certificate holder"));
	}
	sign_options options = {
	    .appearance = {.page_number = 1},
	    .appearance_file = job->image,
	    .appearance_information = job->information
	};
	libssig_pdf_appearance pixels = {0};
	unsigned char*         bytes = NULL;
	bool ok = appearance_preview(&options, svg, &pixels, &bytes);
	g_free(svg);
	arg_free(&settings);
	if (ok) {
		job->ratio = pixels.width / pixels.height;
		job->stamp = sign_pdf_image(&pixels);
		ok = job->stamp != NULL;
	}
	g_free(bytes);
	double width = job->appearance.width;
	if (ok && job->appearance.page_number) {
		double w, h;
		ok = sign_pdf_size(job->pdf, job->appearance.page_number, &w, &h) &&
		     sign_pdf_place(
		         w,
		         h,
		         job->ratio,
		         job->appearance.x,
		         job->appearance.y,
		         job->appearance.width,
		         job->appearance.width / job->ratio,
		         &job->appearance,
		         &error
		     );
	}
	if (ok && job->appearance.page_number && job->information_changed &&
	    fabs(job->appearance.width - width) > 0.001) {
		ok = false;
		g_set_error_literal(
		    &error,
		    SIGN_GUI_ERROR,
		    G_IO_ERROR_INVALID_ARGUMENT,
		    _("The signature image cannot fit at this position")
		);
	}
	if (!error && g_cancellable_set_error_if_cancelled(cancellable, &error)) {
		ok = false;
	}
	if (!ok && !error) {
		g_set_error_literal(
		    &error,
		    SIGN_GUI_ERROR,
		    G_IO_ERROR_INVALID_DATA,
		    _("Cannot preview this signature image")
		);
	}
	if (ok) {
		g_task_return_boolean(task, TRUE);
	} else {
		g_task_return_error(task, error);
	}
}
static void preview_finished(
    GObject*      source,
    GAsyncResult* result,
    gpointer      data
) {
	(void)data;
	SignSigning* self = SIGN_SIGNING(source);
	preview_job* job = g_task_get_task_data(G_TASK(result));
	GError*      error = NULL;
	bool         ok = g_task_propagate_boolean(G_TASK(result), &error);
	if (self->closing || job->revision != self->revision ||
	    self->current != job->item) {
		g_clear_error(&error);
		return;
	}
	self->loading = false;
	if (!ok) {
		report_error(self, error, GUI_ERROR_PDF);
		g_clear_error(&error);
		if (!self->surface) {
			sign_signing_show_queue(self);
		}
	} else if (!job->pdf) {
		self->current->checked = true;
		self->current->is_pdf = false;
		sign_signing_show_queue(self);
	} else {
		signing_item* item = job->item;
		sign_pdf_unref(item->pdf);
		item->pdf = sign_pdf_ref(job->pdf);
		item->checked = true;
		g_free(item->image);
		item->image = g_strdup(job->image);
		g_free(item->profile_image);
		item->profile_image = g_strdup(job->profile_image);
		item->image_set = true;
		item->information = job->information;
		item->appearance = job->appearance;
		if (self->surface) {
			cairo_surface_destroy(self->surface);
		}
		if (self->stamp) {
			cairo_surface_destroy(self->stamp);
		}
		self->surface = job->surface;
		job->surface = NULL;
		self->stamp = job->stamp;
		job->stamp = NULL;
		self->page_number = job->page;
		self->ratio = job->ratio;
		sign_pdf_size(
		    item->pdf,
		    job->page,
		    &self->page_width,
		    &self->page_height
		);
		self->updating = true;
		gtk_spin_button_set_range(self->page, 1, sign_pdf_pages(item->pdf));
		gtk_spin_button_set_value(self->page, job->page);
		self->updating = false;
		char* text = g_strdup_printf(_("of %u"), sign_pdf_pages(item->pdf));
		gtk_label_set_text(self->page_count, text);
		g_free(text);
		sign_pdf_reader_set_document(
		    self->reader,
		    item->pdf,
		    job->page,
		    self->surface
		);
	}
	redraw(self);
}
static void load_preview(
    SignSigning* self,
    unsigned int page,
    const char*  image,
    bool         override,
    bool         information
) {
	if (!self->current || self->busy || self->closing) {
		return;
	}
	if (self->preview) {
		g_cancellable_cancel(self->preview);
		g_object_unref(self->preview);
	}
	self->preview = g_cancellable_new();
	self->loading = true;
	++self->revision;
	preview_job* job = g_new0(preview_job, 1);
	job->item = item_ref(self->current);
	job->pdf = sign_pdf_ref(self->current->pdf);
	job->page = page;
	job->revision = self->revision;
	job->profile = g_strdup(profile(self));
	job->image = g_strdup(override ? image : self->current->image);
	job->image_set = override || self->current->image_set;
	job->override = override;
	job->information = information;
	job->information_changed = information != self->current->information;
	job->appearance = self->current->appearance;
	message(self, NULL);
	redraw(self);
	GTask* task = g_task_new(self, self->preview, preview_finished, NULL);
	g_task_set_task_data(task, job, preview_free);
	g_task_run_in_thread(task, preview_worker);
	g_object_unref(task);
}
size_t sign_signing_count(SignSigning* self) {
	return self->items->len;
}
const char* sign_signing_output(SignSigning* self, size_t index) {
	return index < self->items->len
	           ? ((signing_item*)g_ptr_array_index(self->items, index))->output
	           : NULL;
}
void sign_signing_set_output(
    SignSigning* self,
    size_t       index,
    const char*  output
) {
	if (index >= self->items->len) {
		return;
	}
	set_item_output(self, g_ptr_array_index(self->items, index), output);
	controls(self);
}
bool sign_signing_is_loading(SignSigning* self) {
	return self->loading;
}
bool sign_signing_is_busy(SignSigning* self) {
	return self->busy;
}
bool sign_signing_is_reader(SignSigning* self) {
	return !strcmp(gtk_stack_get_visible_child_name(self->modes), "reader");
}
const libssig_pdf_appearance* sign_signing_appearance(
    SignSigning* self,
    size_t       index
) {
	return index < self->items->len
	           ? &((signing_item*)g_ptr_array_index(self->items, index))
	                  ->appearance
	           : NULL;
}
void sign_signing_open_document(SignSigning* self, size_t index) {
	if (index >= self->items->len || self->busy) {
		return;
	}
	signing_item* item = g_ptr_array_index(self->items, index);
	if (!item->is_pdf || (item->checked && !item->pdf)) {
		return;
	}
	self->current = item;
	GFile* location = g_file_new_for_path(item->path);
	sign_file_actions_attach(
	    GTK_WIDGET(self->reader),
	    location,
	    false,
	    false,
	    NULL,
	    NULL
	);
	sign_file_allow_validation(GTK_WIDGET(self->reader));
	g_object_unref(location);
	sign_pdf_reader_set_document(self->reader, NULL, 0, NULL);
	if (self->surface) {
		cairo_surface_destroy(self->surface);
		self->surface = NULL;
	}
	if (self->stamp) {
		cairo_surface_destroy(self->stamp);
		self->stamp = NULL;
	}
	char* name = g_filename_display_basename(item->path);
	gtk_label_set_text(self->filename, name);
	g_free(name);
	gtk_stack_set_visible_child_name(self->modes, "reader");
	load_preview(
	    self,
	    item->appearance.page_number ? item->appearance.page_number : 1,
	    NULL,
	    false,
	    item->information
	);
}
void sign_signing_show_queue(SignSigning* self) {

	gtk_stack_set_visible_child_name(
	    self->modes,
	    self->items->len ? "queue" : "empty"
	);
	controls(self);
}
void sign_signing_set_page(SignSigning* self, unsigned int page) {
	if (self->current && self->current->pdf && page &&
	    page <= sign_pdf_pages(self->current->pdf)) {
		sign_pdf_reader_goto(self->reader, page);
		reader_page(self->reader, page, self);
	}
}
void sign_signing_set_image(SignSigning* self, const char* file) {
	if (self->current) {
		load_preview(
		    self,
		    self->page_number,
		    file,
		    true,
		    self->current->information
		);
	}
}
static void information_toggled(GtkCheckButton* button, gpointer data) {
	SignSigning* self = data;
	if (!self->updating && self->current && !self->busy && !self->loading) {
		load_preview(
		    self,
		    self->page_number,
		    NULL,
		    false,
		    gtk_check_button_get_active(button)
		);
	}
}
static void page_changed(GtkSpinButton* spin, gpointer data) {
	SignSigning* self = data;
	if (!self->updating) {
		sign_signing_set_page(
		    self,
		    (unsigned int)gtk_spin_button_get_value_as_int(spin)
		);
	}
}
static void continue_clicked(GtkButton* button, gpointer data) {
	(void)button;
	sign_signing_show_queue(data);
}
static void reset_image(GtkButton* button, gpointer data) {
	(void)button;
	SignSigning* self = data;
	if (self->current) {
		self->current->image_set = false;
		load_preview(
		    self,
		    self->page_number,
		    NULL,
		    false,
		    self->current->information
		);
	}
}
void sign_signing_clear(SignSigning* self) {
	if (self->busy || self->closing) {
		return;
	}
	++self->revision;
	if (self->preview) {
		g_cancellable_cancel(self->preview);
		g_object_unref(self->preview);
		self->preview = NULL;
	}
	self->loading = false;
	self->current = NULL;
	self->page_number = 0;
	sign_pdf_reader_set_document(self->reader, NULL, 0, NULL);
	sign_pdf_reader_set_stamp(self->reader, NULL, NULL);
	if (self->surface) {
		cairo_surface_destroy(self->surface);
		self->surface = NULL;
	}
	if (self->stamp) {
		cairo_surface_destroy(self->stamp);
		self->stamp = NULL;
	}
	GtkWidget* row;
	while ((row = gtk_widget_get_first_child(GTK_WIDGET(self->list)))) {
		gtk_list_box_remove(self->list, row);
	}
	g_ptr_array_set_size(self->items, 0);
	refresh_output_warnings(self);
	message(self, NULL);
	sign_signing_show_queue(self);
}
static void clear_clicked(GtkButton* button, gpointer data) {
	(void)button;
	sign_signing_clear(data);
}
static void discard_document(SignSigning* self, guint index) {
	signing_item* item = g_ptr_array_index(self->items, index);
	if (self->current == item) {
		self->current = NULL;
		++self->revision;
		if (self->preview) {
			g_cancellable_cancel(self->preview);
		}
		self->loading = false;
	}
	gtk_list_box_remove(self->list, item->row);
	g_ptr_array_remove_index(self->items, index);
	refresh_output_warnings(self);
	sign_signing_show_queue(self);
}
static void remove_item(GtkButton* button, gpointer data) {
	SignSigning*  self = data;
	signing_item* item = g_object_get_data(G_OBJECT(button), "signing-item");
	guint         index;
	if (!self->busy && g_ptr_array_find(self->items, item, &index)) {
		discard_document(self, index);
	}
}
static void prune_documents(SignSigning* self) {
	if (self->busy || self->closing) {
		return;
	}
	for (guint i = self->items->len; i > 0; --i) {
		signing_item* item = g_ptr_array_index(self->items, i - 1);
		GFile*        file = g_file_new_for_path(item->path);
		if (file_is_missing(file)) {
			discard_document(self, i - 1);
		}
		g_object_unref(file);
	}
}
static void graphical_clicked(GtkButton* button, gpointer data) {
	signing_item* item = g_object_get_data(G_OBJECT(button), "signing-item");
	SignSigning*  self = data;
	for (guint i = 0; i < self->items->len; ++i) {
		if (g_ptr_array_index(self->items, i) == item) {
			sign_signing_open_document(self, i);
			break;
		}
	}
}
static void row_activated(GtkListBox* list, GtkListBoxRow* row, gpointer data) {
	(void)list;
	sign_signing_open_document(data, (size_t)gtk_list_box_row_get_index(row));
}
static void update_item_output_row(signing_item* item) {
	if (!item->row) {
		return;
	}
	char* display = item->output ? g_filename_display_name(item->output)
	                             : g_strdup(_("Unavailable"));
	char* text = g_strdup_printf(_("Output: %s"), display);
	adw_action_row_set_subtitle(ADW_ACTION_ROW(item->row), text);
	adw_action_row_set_subtitle_lines(ADW_ACTION_ROW(item->row), 1);
	gtk_widget_set_tooltip_text(item->row, item->path);
	g_free(text);
	g_free(display);
}
static void set_item_output(
    SignSigning*  self,
    signing_item* item,
    const char*   output
) {
	if (!g_strcmp0(item->output, output)) {
		return;
	}
	g_free(item->output);
	item->output = g_strdup(output);
	update_item_output_row(item);
	refresh_output_warnings(self);
}
void sign_signing_add_files(SignSigning* self, GListModel* files) {
	if (self->busy || self->closing) {
		return;
	}
	prune_documents(self);
	guint added = 0;
	for (guint n = 0; n < g_list_model_get_n_items(files); ++n) {
		GFile* file = g_list_model_get_item(files, n);
		if (!G_IS_FILE(file)) {
			if (file) {
				g_object_unref(file);
			}
			continue;
		}
		if (file_is_missing(file)) {
			g_object_unref(file);
			continue;
		}
		char* path = g_file_get_path(file);
		g_object_unref(file);
		if (!path) {
			message(self, _("Only local files can be signed"));
			continue;
		}
		for (guint i = self->items->len; i > 0; --i) {
			signing_item* previous = g_ptr_array_index(self->items, i - 1);
			GFile*        old = g_file_new_for_path(previous->path);
			GFile*        fresh = g_file_new_for_path(path);
			bool          same = g_file_equal(old, fresh);
			g_object_unref(old);
			g_object_unref(fresh);
			if (same) {
				discard_document(self, i - 1);
			}
		}
		if (self->items->len >= SIGN_FLOW_MAX_DOCUMENTS) {
			g_free(path);
			message(self, _("The document queue is full"));
			break;
		}
		char* output = file_signed_path(path);
		if (!output) {
			g_free(path);
			message(self, _("Cannot prepare the output path"));
			continue;
		}
		signing_item* item = g_new0(signing_item, 1);
		item->references = 1;
		item->information = true;
		item->path = path;
		item->is_pdf = file_is_pdf(path);
		item->output = output;
		item->row = adw_action_row_new();
		adw_preferences_row_set_use_markup(
		    ADW_PREFERENCES_ROW(item->row),
		    FALSE
		);
		char* name = g_filename_display_basename(path);
		adw_preferences_row_set_title(ADW_PREFERENCES_ROW(item->row), name);
		g_free(name);
		update_item_output_row(item);
		gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(item->row), TRUE);
		item->warning = gtk_image_new_from_icon_name("dialog-warning-symbolic");
		gtk_widget_set_name(item->warning, "sign-document-warning");
		gtk_widget_add_css_class(item->warning, "warning");
		adw_action_row_add_suffix(ADW_ACTION_ROW(item->row), item->warning);
		if (item->is_pdf) {
			GtkWidget* graphical = gtk_button_new_from_icon_name(
			    "insert-image-symbolic"
			);
			gtk_widget_set_name(graphical, "sign-graphical-signature");
			gtk_widget_set_tooltip_text(graphical, _("Graphical signature"));
			gtk_widget_add_css_class(graphical, "flat");
			gtk_widget_set_valign(graphical, GTK_ALIGN_CENTER);
			g_object_set_data(G_OBJECT(graphical), "signing-item", item);
			g_signal_connect(
			    graphical,
			    "clicked",
			    G_CALLBACK(graphical_clicked),
			    self
			);
			adw_action_row_add_suffix(ADW_ACTION_ROW(item->row), graphical);
		}
		item->output_button = gtk_button_new_from_icon_name(
		    "document-edit-symbolic"
		);
		gtk_widget_set_name(item->output_button, "sign-change-output");
		gtk_widget_add_css_class(item->output_button, "flat");
		gtk_widget_set_valign(item->output_button, GTK_ALIGN_CENTER);
		gtk_widget_set_tooltip_text(
		    item->output_button,
		    _("Change the output file")
		);
		g_object_set_data(G_OBJECT(item->output_button), "signing-item", item);
		g_signal_connect(
		    item->output_button,
		    "clicked",
		    G_CALLBACK(change_output_clicked),
		    self
		);
		adw_action_row_add_suffix(
		    ADW_ACTION_ROW(item->row),
		    item->output_button
		);
		GtkWidget* remove = gtk_button_new_from_icon_name(
		    "window-close-symbolic"
		);
		gtk_widget_add_css_class(remove, "flat");
		gtk_widget_set_valign(remove, GTK_ALIGN_CENTER);
		gtk_widget_set_tooltip_text(remove, _("Remove document"));
		g_object_set_data(G_OBJECT(remove), "signing-item", item);
		g_signal_connect(remove, "clicked", G_CALLBACK(remove_item), self);
		gtk_widget_set_name(remove, "sign-remove-document");
		GFile* location = g_file_new_for_path(item->path);
		sign_file_actions_attach(item->row, location, false, false, NULL, NULL);
		sign_file_allow_validation(item->row);
		g_object_unref(location);
		adw_action_row_add_suffix(ADW_ACTION_ROW(item->row), remove);
		gtk_list_box_append(self->list, item->row);
		g_ptr_array_add(self->items, item);
		++added;
	}
	refresh_output_warnings(self);
	sign_signing_show_queue(self);
	if (added == 1 && self->items->len == 1) {
		sign_signing_open_document(self, 0);
	}
}
static GtkWindow* parent_window(SignSigning* self) {
	GtkRoot* root = gtk_widget_get_root(GTK_WIDGET(self));
	return GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : NULL;
}
static void files_selected(
    GObject*      source,
    GAsyncResult* result,
    gpointer      data
) {
	SignSigning* self = data;
	GError*      error = NULL;
	GListModel*  files = gtk_file_dialog_open_multiple_finish(
        GTK_FILE_DIALOG(source),
        result,
        &error
    );
	if (files) {
		sign_signing_add_files(self, files);
		g_object_unref(files);
	} else if (!self->closing &&
	           !g_error_matches(
	               error,
	               GTK_DIALOG_ERROR,
	               GTK_DIALOG_ERROR_DISMISSED
	           ) &&
	           !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		report_error(self, error, GUI_ERROR_FILES);
	}
	g_clear_error(&error);
	g_object_unref(self);
}
static void choose_files(GtkButton* button, gpointer data) {
	(void)button;
	SignSigning*   self = data;
	GtkFileDialog* dialog = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dialog, _("Choose documents to sign"));
	gtk_file_dialog_open_multiple(
	    dialog,
	    parent_window(self),
	    self->lifetime,
	    files_selected,
	    g_object_ref(self)
	);
	g_object_unref(dialog);
}
static void image_selected(
    GObject*      source,
    GAsyncResult* result,
    gpointer      data
) {
	SignSigning* self = data;
	GError*      error = NULL;
	GFile*       file = gtk_file_dialog_open_finish(
        GTK_FILE_DIALOG(source),
        result,
        &error
    );
	if (file && !self->closing) {
		char* path = g_file_get_path(file);
		if (path) {
			sign_signing_set_image(self, path);
		} else {
			message(self, _("Only local images are supported"));
		}
		g_free(path);
	} else if (error && !self->closing &&
	           !g_error_matches(
	               error,
	               GTK_DIALOG_ERROR,
	               GTK_DIALOG_ERROR_DISMISSED
	           ) &&
	           !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		report_error(self, error, GUI_ERROR_FILES);
	}
	if (file) {
		g_object_unref(file);
	}
	g_clear_error(&error);
	g_object_unref(self);
}
static void choose_image(GtkButton* button, gpointer data) {
	(void)button;
	SignSigning*   self = data;
	GtkFileDialog* dialog = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dialog, _("Choose signature image"));
	GtkFileFilter* filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, _("Signature images"));
	GSList* formats = gdk_pixbuf_get_formats();
	for (GSList* node = formats; node; node = node->next) {
		char** types = gdk_pixbuf_format_get_mime_types(node->data);
		for (size_t n = 0; types[n]; ++n) {
			gtk_file_filter_add_mime_type(filter, types[n]);
		}
		g_strfreev(types);
	}
	g_slist_free(formats);
	gtk_file_filter_add_mime_type(filter, "image/svg+xml");
	GListStore* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
	g_list_store_append(filters, filter);
	gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
	g_object_unref(filters);
	gtk_file_dialog_set_default_filter(dialog, filter);
	g_object_unref(filter);
	gtk_file_dialog_open(
	    dialog,
	    parent_window(self),
	    self->lifetime,
	    image_selected,
	    g_object_ref(self)
	);
	g_object_unref(dialog);
}
static void files_dropped(GtkWidget* widget, GListModel* files, gpointer data) {
	(void)data;
	sign_signing_add_files(SIGN_SIGNING(widget), files);
}
static GCancellable* drop_cancel(GtkWidget* widget, gpointer data) {
	(void)data;
	return SIGN_SIGNING(widget)->lifetime;
}
static void drop_state(
    GtkWidget*    widget,
    bool          pending,
    const GError* error,
    gpointer      data
) {
	(void)pending;
	(void)data;
	SignSigning* self = SIGN_SIGNING(widget);
	if (!self->closing && error &&
	    !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		report_error(self, error, GUI_ERROR_FILES);
	}
}

static void profile_changed(GObject* object, GParamSpec* pspec, gpointer data) {
	(void)object;
	(void)pspec;
	SignSigning* self = data;
	if (!self->updating && !self->busy && self->current &&
	    !self->current->appearance.page_number) {
		self->current->image_set = false;
		if (sign_signing_is_reader(self)) {
			load_preview(
			    self,
			    self->page_number ? self->page_number : 1,
			    NULL,
			    false,
			    self->current->information
			);
		}
	}
}
void sign_signing_reload_profiles(SignSigning* self) {
	if (self->busy) {
		return;
	}
	prune_documents(self);
	char*             previous = g_strdup(profile(self));
	GError*           error = NULL;
	sign_preferences* preferences = preferences_load(&error);
	if (!preferences) {
		report_error(self, error, GUI_ERROR_SETTINGS);
		g_clear_error(&error);
		g_free(previous);
		return;
	}
	GtkStringList* list = gtk_string_list_new(NULL);
	gtk_string_list_append(list, _("Global"));
	self->updating = true;
	g_ptr_array_set_size(self->profile_ids, 0);
	g_ptr_array_add(self->profile_ids, NULL);
	guint selected = 0;
	for (size_t n = 0; n < preferences_count(preferences); ++n) {
		sign_preferences_section* section = preferences_profile(preferences, n);
		GVariant *                id = preferences_get(section, "label"),
		         *name = preferences_get(section, "name");
		const char* label = id ? g_variant_get_string(id, NULL) : NULL;
		if (!label || !*label) {
			continue;
		}
		const char* display = name ? g_variant_get_string(name, NULL) : NULL;
		gtk_string_list_append(list, display && *display ? display : label);
		g_ptr_array_add(self->profile_ids, g_strdup(label));
		if (previous && !strcmp(previous, label)) {
			selected = self->profile_ids->len - 1;
		}
	}
	gtk_drop_down_set_model(self->profiles, G_LIST_MODEL(list));
	gtk_drop_down_set_selected(self->profiles, selected);
	self->updating = false;
	g_object_unref(list);
	preferences_free(preferences);
	g_free(previous);
}
GtkWidget* sign_signing_profile_selector(SignSigning* self) {
	return GTK_WIDGET(self->profiles);
}
static void profile_setup(
    GtkSignalListItemFactory* factory,
    GtkListItem*              item,
    gpointer                  data
) {
	(void)factory;
	(void)data;
	GtkWidget* label = gtk_label_new(NULL);
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
	gtk_label_set_max_width_chars(GTK_LABEL(label), 24);
	gtk_label_set_width_chars(GTK_LABEL(label), 8);
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_list_item_set_child(item, label);
}
static void profile_bind(
    GtkSignalListItemFactory* factory,
    GtkListItem*              item,
    gpointer                  data
) {
	(void)factory;
	(void)data;
	gtk_label_set_text(
	    GTK_LABEL(gtk_list_item_get_child(item)),
	    gtk_string_object_get_string(
	        GTK_STRING_OBJECT(gtk_list_item_get_item(item))
	    )
	);
}
static sign_flow* build_items(
    GPtrArray*  items,
    const char* label,
    sign_args*  settings,
    bool        overwrite,
    GError**    error
) {
	char detail[512];
	if (!config_load(settings, label, detail, sizeof(detail))) {
		g_set_error_literal(
		    error,
		    SIGN_GUI_ERROR,
		    G_IO_ERROR_INVALID_DATA,
		    _("The signing profile could not be loaded. Check the "
		      "configuration in Settings.")
		);
		return NULL;
	}
	sign_flow_item* documents = g_new0(sign_flow_item, items->len);
	for (guint n = 0; n < items->len; ++n) {
		signing_item* item = g_ptr_array_index(items, n);
		documents[n].operation = SIGN_FLOW_SIGN;
		documents[n].options = settings->options;
		sign_options* options = &documents[n].options;
		options->input = item->path;
		options->output = item->output;
		options->overwrite = overwrite;
		options->appearance = item->appearance;
		options->appearance_information = item->information;
		options->appearance_file = item->appearance.page_number ? item->image
		                                                        : NULL;
		options->appearance_options = item->appearance.page_number
		                                  ? SIGN_APPEARANCE_X |
		                                        SIGN_APPEARANCE_Y |
		                                        SIGN_APPEARANCE_WIDTH
		                                  : 0;
		if (item->appearance.page_number) {
			options->format = LIBSSIG_FORMAT_PADES;
			options->format_explicit = true;
		}
	}
	sign_flow* flow = sign_flow_from_items(documents, items->len, error);
	g_free(documents);
	return flow;
}
sign_flow* sign_signing_build_flow(SignSigning* self, GError** error) {
	sign_args settings = {0};
	sign_flow*
	    flow = build_items(self->items, profile(self), &settings, false, error);
	arg_free(&settings);
	return flow;
}

typedef struct {
	gint          references;
	SignSigning*  self;
	GCancellable* cancellable;
	GMutex        lock;
	GCond         condition;
	bool          answered, accepted;
	char*         token;
	sign_buffer   pin;
	GtkWidget*    entry;
} pin_request;
static pin_request* pin_ref(pin_request* request) {
	g_atomic_int_inc(&request->references);
	return request;
}
static void pin_unref(gpointer data) {
	pin_request* request = data;
	if (!g_atomic_int_dec_and_test(&request->references)) {
		return;
	}
	pin_free(&request->pin);
	g_free(request->token);
	g_object_unref(request->self);
	g_object_unref(request->cancellable);
	g_mutex_clear(&request->lock);
	g_cond_clear(&request->condition);
	g_free(request);
}
static void pin_closure_free(gpointer data, GClosure* closure) {
	(void)closure;
	pin_unref(data);
}
static void pin_answer(pin_request* request, bool accepted) {
	pin_ref(request);
	g_mutex_lock(&request->lock);
	if (!request->answered) {
		const char* text = accepted ? gtk_editable_get_text(
		                                  GTK_EDITABLE(request->entry)
		                              )
		                            : "";
		size_t      length = strlen(text);
		request->accepted = accepted && length <= SIGN_PIN_LIMIT;
		if (request->accepted) {
			request->pin.data = malloc(length + 1);
			request->accepted = request->pin.data != NULL;
			if (request->accepted) {
				memcpy(request->pin.data, text, length + 1);
				request->pin.len = length;
			}
		}
		gtk_editable_set_text(GTK_EDITABLE(request->entry), "");
		request->answered = true;
		g_cond_signal(&request->condition);
	}
	g_mutex_unlock(&request->lock);
	if (request->self->pin_window) {
		gtk_window_destroy(request->self->pin_window);
	}
	pin_unref(request);
}
static void pin_confirm(GtkWidget* widget, gpointer data) {
	(void)widget;
	pin_answer(data, true);
}
static void pin_cancel(GtkButton* widget, gpointer data) {
	(void)widget;
	pin_answer(data, false);
}
static gboolean pin_close(GtkWindow* window, gpointer data) {
	(void)window;
	pin_answer(data, false);
	return TRUE;
}
static gboolean show_pin(gpointer data) {
	pin_request* request = data;
	SignSigning* self = request->self;
	if (self->closing || g_cancellable_is_cancelled(request->cancellable) ||
	    !parent_window(self)) {
		g_mutex_lock(&request->lock);
		request->answered = true;
		g_cond_signal(&request->condition);
		g_mutex_unlock(&request->lock);
		return G_SOURCE_REMOVE;
	}
	GtkWidget* window = gtk_window_new();
	self->pin_window = GTK_WINDOW(window);
	g_object_add_weak_pointer(G_OBJECT(window), (gpointer*)&self->pin_window);
	gtk_window_set_transient_for(GTK_WINDOW(window), parent_window(self));
	gtk_window_set_application(
	    GTK_WINDOW(window),
	    gtk_window_get_application(parent_window(self))
	);
	gtk_window_set_destroy_with_parent(GTK_WINDOW(window), TRUE);
	gtk_window_set_modal(GTK_WINDOW(window), TRUE);
	gtk_window_set_title(GTK_WINDOW(window), _("Token PIN"));
	gtk_window_set_default_size(GTK_WINDOW(window), 360, -1);
	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
	gtk_widget_set_margin_start(box, 24);
	gtk_widget_set_margin_end(box, 24);
	gtk_widget_set_margin_top(box, 24);
	gtk_widget_set_margin_bottom(box, 24);
	GtkWidget* title = gtk_label_new(_("Enter the PIN to sign"));
	gtk_widget_add_css_class(title, "title-2");
	gtk_box_append(GTK_BOX(box), title);
	GtkWidget* token = gtk_label_new(request->token);
	gtk_label_set_wrap(GTK_LABEL(token), TRUE);
	gtk_label_set_selectable(GTK_LABEL(token), TRUE);
	gtk_box_append(GTK_BOX(box), token);
	request->entry = gtk_password_entry_new();
	gtk_password_entry_set_show_peek_icon(
	    GTK_PASSWORD_ENTRY(request->entry),
	    TRUE
	);
	gtk_widget_set_name(request->entry, "sign-pin-entry");
	gtk_widget_set_tooltip_text(request->entry, _("PIN"));
	gtk_box_append(GTK_BOX(box), request->entry);
	GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_widget_set_halign(actions, GTK_ALIGN_END);
	GtkWidget* cancel = gtk_button_new_with_label(_("Cancel"));
	GtkWidget* confirm = gtk_button_new_with_label(_("Continue"));
	gtk_widget_set_name(confirm, "sign-pin-confirm");
	gtk_widget_set_name(cancel, "sign-pin-cancel");
	gtk_widget_add_css_class(confirm, "suggested-action");
	g_signal_connect_data(
	    confirm,
	    "clicked",
	    G_CALLBACK(pin_confirm),
	    pin_ref(request),
	    pin_closure_free,
	    0
	);
	g_signal_connect_data(
	    cancel,
	    "clicked",
	    G_CALLBACK(pin_cancel),
	    pin_ref(request),
	    pin_closure_free,
	    0
	);
	g_signal_connect_data(
	    request->entry,
	    "activate",
	    G_CALLBACK(pin_confirm),
	    pin_ref(request),
	    pin_closure_free,
	    0
	);
	g_signal_connect_data(
	    window,
	    "close-request",
	    G_CALLBACK(pin_close),
	    pin_ref(request),
	    pin_closure_free,
	    0
	);
	gtk_box_append(GTK_BOX(actions), cancel);
	gtk_box_append(GTK_BOX(actions), confirm);
	gtk_box_append(GTK_BOX(box), actions);
	gtk_window_set_child(GTK_WINDOW(window), box);
	gtk_window_present(GTK_WINDOW(window));
	gtk_widget_grab_focus(request->entry);
	return G_SOURCE_REMOVE;
}
static bool copy_pin(const sign_buffer* source, sign_buffer* dest) {
	dest->data = malloc(source->len + 1);
	if (!dest->data) {
		return false;
	}
	memcpy(dest->data, source->data, source->len);
	dest->data[source->len] = 0;
	dest->len = source->len;
	return true;
}
typedef struct {
	GPtrArray*       items;
	char *           profile, *cached_uri;
	sign_buffer      cached_pin;
	sign_args        settings;
	sign_flow*       flow;
	sign_flow_result result;
	GApplication*    application;
	SignSigning*     self;
	GCancellable*    cancellable;
	bool             overwrite;
} signing_run;
static bool request_pin(
    const libssig_token* token,
    sign_buffer*         pin,
    void*                data,
    GError**             error
) {
	signing_run* run = data;
	if (g_cancellable_set_error_if_cancelled(run->cancellable, error)) {
		return false;
	}
	bool ok = false;
	if (run->cached_uri && !strcmp(run->cached_uri, token->uri)) {
		ok = copy_pin(&run->cached_pin, pin);
	} else if (run->settings.pincmd && run->settings.pincmd[0]) {
		ok = pin_read(run->settings.pincmd, pin);
	} else {
		pin_request* request = g_new0(pin_request, 1);
		request->references = 1;
		request->self = g_object_ref(run->self);
		request->cancellable = g_object_ref(run->cancellable);
		request->token = g_utf8_make_valid(
		    token->label ? token->label : "",
		    -1
		);
		g_mutex_init(&request->lock);
		g_cond_init(&request->condition);
		GSource* source = g_idle_source_new();
		g_source_set_callback(source, show_pin, pin_ref(request), pin_unref);
		g_source_attach(source, g_main_context_default());
		g_source_unref(source);
		g_mutex_lock(&request->lock);
		while (!request->answered &&
		       !g_cancellable_is_cancelled(run->cancellable)) {
			g_cond_wait_until(
			    &request->condition,
			    &request->lock,
			    g_get_monotonic_time() + 100 * G_TIME_SPAN_MILLISECOND
			);
		}
		ok = request->answered && request->accepted &&
		     !g_cancellable_is_cancelled(run->cancellable);
		if (ok) {
			*pin = request->pin;
			request->pin = (sign_buffer){0};
		}
		g_mutex_unlock(&request->lock);
		pin_unref(request);
	}
	if (ok && !run->cached_uri) {
		run->cached_uri = g_strdup(token->uri);
		ok = copy_pin(pin, &run->cached_pin);
	}
	if (g_cancellable_set_error_if_cancelled(run->cancellable, error)) {
		ok = false;
	}
	if (!ok) {
		pin_free(pin);
		if (!error || !*error) {
			g_set_error_literal(
			    error,
			    SIGN_GUI_ERROR,
			    G_IO_ERROR_CANCELLED,
			    _("PIN entry was cancelled or unavailable")
			);
		}
	}
	return ok;
}
static void run_free(gpointer data) {
	signing_run* run = data;
	g_ptr_array_unref(run->items);
	g_free(run->profile);
	g_free(run->cached_uri);
	pin_free(&run->cached_pin);
	arg_free(&run->settings);
	sign_flow_free(run->flow);
	sign_flow_result_clear(&run->result);
	if (run->application) {
		g_object_unref(run->application);
	}
	g_object_unref(run->cancellable);
	g_free(run);
}
static void run_worker(
    GTask*        task,
    gpointer      source,
    gpointer      data,
    GCancellable* cancellable
) {
	(void)source;
	signing_run* run = data;
	GError*      error = NULL;
	run->flow = build_items(
	    run->items,
	    run->profile,
	    &run->settings,
	    run->overwrite,
	    &error
	);
	bool ok = run->flow != NULL;
	for (size_t n = 0; ok && !run->overwrite && n < sign_flow_count(run->flow);
	     ++n) {
		const sign_options* options = sign_flow_document(run->flow, n);
		GFile*              output = g_file_new_for_path(options->output);
		bool                exists = g_file_query_exists(output, cancellable);
		g_object_unref(output);
		if (exists) {
			g_set_error(
			    &error,
			    SIGN_GUI_ERROR,
			    G_IO_ERROR_EXISTS,
			    _("Output already exists: %s"),
			    options->output
			);
			ok = false;
		}
	}
	sign_execution execution = {
	    .request_pin = request_pin,
	    .user_data = run,
	    .cancellable = cancellable
	};
	if (ok) {
		sign_device_access_writer_lock();
		ok = sign_flow_run(run->flow, &execution, &run->result, &error);
		sign_device_access_writer_unlock();
	}
	/* The only retained PIN copy lasts for this execution, never the UI/JSON.
	 */
	pin_free(&run->cached_pin);
	if (ok) {
		g_task_return_boolean(task, TRUE);
	} else {
		if (!error) {
			g_set_error_literal(
			    &error,
			    SIGN_GUI_ERROR,
			    G_IO_ERROR_FAILED,
			    _("Signing failed")
			);
		}
		g_task_return_error(task, error);
	}
}
static sign_flow* completed_verification(
    const sign_flow* signed_flow,
    GError**         error
) {
	size_t          count = sign_flow_count(signed_flow);
	sign_flow_item* documents = g_new0(sign_flow_item, count);
	for (size_t n = 0; n < count; ++n) {
		const sign_options* source = sign_flow_document(signed_flow, n);
		documents[n].operation = SIGN_FLOW_VERIFY;
		documents[n].options = *source;
		documents[n].options.input = source->output;
		documents[n].content = source->detached ? source->input : NULL;
	}
	sign_flow* flow = sign_flow_from_items(documents, count, error);
	g_free(documents);
	return flow;
}
static void run_finished(GObject* source, GAsyncResult* result, gpointer data) {
	(void)data;
	SignSigning* self = SIGN_SIGNING(source);
	signing_run* run = g_task_get_task_data(G_TASK(result));
	GError*      error = NULL;
	bool         ok = g_task_propagate_boolean(G_TASK(result), &error);
	self->busy = false;
	if (!self->closing) {
		for (size_t n = 0; n < run->result.count; ++n) {
			signing_item*              item = g_ptr_array_index(run->items, n);
			sign_flow_document_result* report = &run->result.documents[n];
			if (!report->ok) {
				adw_action_row_set_subtitle(
				    ADW_ACTION_ROW(item->row),
				    report->attempted ? _("Signing failed") : _("Not started")
				);
			}
		}
		if (ok) {
			message(self, NULL);
		} else {
			char* text = gui_error_message(error, GUI_ERROR_SIGNING);
			message(self, NULL);
			if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
			    !g_error_matches(error, SIGN_GUI_ERROR, G_IO_ERROR_CANCELLED)) {
				gui_error_dialog(
				    parent_window(self),
				    _("Could not sign documents"),
				    text
				);
			}
			g_free(text);
		}
		refresh_output_warnings(self);
		controls(self);
		if (run->result.completed) {
			g_signal_emit(self, documents_signed, 0);
		}
		if (ok) {
			GError*    handoff_error = NULL;
			sign_flow* verification = completed_verification(
			    run->flow,
			    &handoff_error
			);
			if (verification) {
				g_signal_emit(self, signing_completed, 0, verification);
				sign_flow_free(verification);
			} else {
				char* text = gui_error_message(
				    handoff_error,
				    GUI_ERROR_VALIDATION
				);
				gui_error_dialog(
				    parent_window(self),
				    _("Could not validate signed documents"),
				    text
				);
				g_free(text);
				g_clear_error(&handoff_error);
			}
		}
	}
	if (run->application) {
		g_application_release(run->application);
	}
	g_clear_error(&error);
}
typedef struct {
	SignSigning* self;
	GArray*      indices;
	guint        position;
	bool         run_after;
} name_request;
typedef struct {
	SignSigning* self;
	bool         run_after;
} folder_request;
typedef struct {
	SignSigning* self;
	GArray*      indices;
	bool         hard;
} conflict_request;
static GArray* copy_indices(const GArray* source) {
	GArray* copy = g_array_sized_new(
	    FALSE,
	    FALSE,
	    sizeof(guint),
	    source ? source->len : 0
	);
	if (source && source->len) {
		g_array_append_vals(copy, source->data, source->len);
	}
	return copy;
}
static void name_request_free(name_request* request) {
	g_array_unref(request->indices);
	g_object_unref(request->self);
	g_free(request);
}
static void start_next_name(name_request* request);
static void name_finished(
    GObject*      source,
    GAsyncResult* result,
    gpointer      data
) {
	name_request* request = data;
	SignSigning*  self = request->self;
	GError*       error = NULL;
	GFile*        file = gtk_file_dialog_save_finish(
        GTK_FILE_DIALOG(source),
        result,
        &error
    );
	if (!file) {
		if (!self->closing &&
		    !g_error_matches(
		        error,
		        GTK_DIALOG_ERROR,
		        GTK_DIALOG_ERROR_DISMISSED
		    ) &&
		    !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			report_error(self, error, GUI_ERROR_FILES);
		}
		g_clear_error(&error);
		name_request_free(request);
		return;
	}
	char* path = g_file_get_path(file);
	g_object_unref(file);
	if (path && request->position < request->indices->len) {
		guint index = g_array_index(request->indices, guint, request->position);
		signing_item* item = index < self->items->len
		                         ? g_ptr_array_index(self->items, index)
		                         : NULL;
		if (item) {
			set_item_output(self, item, path);
		}
	}
	g_free(path);
	++request->position;
	start_next_name(request);
}
static void start_next_name(name_request* request) {
	SignSigning* self = request->self;
	if (self->closing || request->position >= request->indices->len) {
		if (request->run_after && !self->closing) {
			sign_signing_run(self);
		}
		name_request_free(request);
		return;
	}
	guint index = g_array_index(request->indices, guint, request->position);
	if (index >= self->items->len) {
		++request->position;
		start_next_name(request);
		return;
	}
	signing_item*  item = g_ptr_array_index(self->items, index);
	GtkFileDialog* dialog = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dialog, _("Choose the output file"));
	if (item->output) {
		char* name = g_path_get_basename(item->output);
		gtk_file_dialog_set_initial_name(dialog, name);
		g_free(name);
		GFile* initial = g_file_new_for_path(item->output);
		gtk_file_dialog_set_initial_file(dialog, initial);
		g_object_unref(initial);
	}
	gtk_file_dialog_save(
	    dialog,
	    parent_window(self),
	    self->lifetime,
	    name_finished,
	    request
	);
	g_object_unref(dialog);
}
static void choose_output_names(
    SignSigning* self,
    GArray*      indices,
    bool         run_after
) {
	if (!indices->len) {
		g_array_unref(indices);
		if (run_after) {
			sign_signing_run(self);
		}
		return;
	}
	name_request* request = g_new0(name_request, 1);
	request->self = g_object_ref(self);
	request->indices = indices;
	request->run_after = run_after;
	start_next_name(request);
}
static void choose_all_output_names(SignSigning* self) {
	GArray* indices = g_array_new(FALSE, FALSE, sizeof(guint));
	for (guint n = 0; n < self->items->len; ++n) {
		g_array_append_val(indices, n);
	}
	choose_output_names(self, indices, false);
}
static void change_output_clicked(GtkButton* button, gpointer data) {
	SignSigning*  self = data;
	signing_item* item = g_object_get_data(G_OBJECT(button), "signing-item");
	guint         index;
	if (self->busy || self->closing || !item ||
	    !g_ptr_array_find(self->items, item, &index)) {
		return;
	}
	GArray* indices = g_array_new(FALSE, FALSE, sizeof(guint));
	g_array_append_val(indices, index);
	choose_output_names(self, indices, false);
}
static void folder_finished(
    GObject*      source,
    GAsyncResult* result,
    gpointer      data
) {
	folder_request* request = data;
	SignSigning*    self = request->self;
	GError*         error = NULL;
	GFile*          folder = gtk_file_dialog_select_folder_finish(
        GTK_FILE_DIALOG(source),
        result,
        &error
    );
	if (!folder) {
		if (!self->closing &&
		    !g_error_matches(
		        error,
		        GTK_DIALOG_ERROR,
		        GTK_DIALOG_ERROR_DISMISSED
		    ) &&
		    !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			report_error(self, error, GUI_ERROR_FILES);
		}
		g_clear_error(&error);
	} else {
		char* directory = g_file_get_path(folder);
		g_object_unref(folder);
		if (directory) {
			for (guint n = 0; n < self->items->len; ++n) {
				signing_item* item = g_ptr_array_index(self->items, n);
				char*         signed_path = file_signed_path(item->path);
				if (!signed_path) {
					continue;
				}
				char* base = g_path_get_basename(signed_path);
				char* output = g_build_filename(directory, base, NULL);
				set_item_output(self, item, output);
				g_free(output);
				g_free(base);
				g_free(signed_path);
			}
		}
		g_free(directory);
	}
	if (request->run_after && !self->closing) {
		sign_signing_run(self);
	}
	g_object_unref(self);
	g_free(request);
}
static void choose_output_folder(SignSigning* self, bool run_after) {
	if (self->closing || !self->items->len) {
		return;
	}
	GtkFileDialog* dialog = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dialog, _("Choose the output folder"));
	if (self->items->len) {
		signing_item* item = g_ptr_array_index(self->items, 0);
		if (item->output) {
			GFile* initial = g_file_new_for_path(item->output);
			GFile* parent = g_file_get_parent(initial);
			if (parent) {
				gtk_file_dialog_set_initial_folder(dialog, parent);
				g_object_unref(parent);
			}
			g_object_unref(initial);
		}
	}
	folder_request* request = g_new0(folder_request, 1);
	request->self = g_object_ref(self);
	request->run_after = run_after;
	gtk_file_dialog_select_folder(
	    dialog,
	    parent_window(self),
	    self->lifetime,
	    folder_finished,
	    request
	);
	g_object_unref(dialog);
}
static void output_folder_clicked(GtkButton* button, gpointer data) {
	(void)button;
	SignSigning* self = data;
	if (self->output_options) {
		GtkPopover* popover = gtk_menu_button_get_popover(
		    GTK_MENU_BUTTON(self->output_options)
		);
		if (popover) {
			gtk_popover_popdown(popover);
		}
	}
	choose_output_folder(self, false);
}
static void output_names_clicked(GtkButton* button, gpointer data) {
	(void)button;
	SignSigning* self = data;
	if (self->output_options) {
		GtkPopover* popover = gtk_menu_button_get_popover(
		    GTK_MENU_BUTTON(self->output_options)
		);
		if (popover) {
			gtk_popover_popdown(popover);
		}
	}
	choose_all_output_names(self);
}
static void conflict_free(conflict_request* request) {
	g_array_unref(request->indices);
	g_object_unref(request->self);
	g_free(request);
}
static void conflict_cancel(GtkButton* button, gpointer data) {
	(void)button;
	gtk_window_destroy(GTK_WINDOW(data));
}
static void conflict_names(GtkButton* button, gpointer data) {
	(void)button;
	GtkWidget*        window = data;
	conflict_request* request = g_object_get_data(
	    G_OBJECT(window),
	    "sign-conflict-request"
	);
	SignSigning* self = g_object_ref(request->self);
	GArray*      indices = copy_indices(request->indices);
	gtk_window_destroy(GTK_WINDOW(window));
	choose_output_names(self, indices, true);
	g_object_unref(self);
}
static void conflict_folder(GtkButton* button, gpointer data) {
	(void)button;
	GtkWidget*        window = data;
	conflict_request* request = g_object_get_data(
	    G_OBJECT(window),
	    "sign-conflict-request"
	);
	SignSigning* self = g_object_ref(request->self);
	gtk_window_destroy(GTK_WINDOW(window));
	choose_output_folder(self, true);
	g_object_unref(self);
}
static void conflict_overwrite(GtkButton* button, gpointer data) {
	(void)button;
	GtkWidget*        window = data;
	conflict_request* request = g_object_get_data(
	    G_OBJECT(window),
	    "sign-conflict-request"
	);
	if (request->hard) {
		return;
	}
	SignSigning* self = g_object_ref(request->self);
	gtk_window_destroy(GTK_WINDOW(window));
	start_run(self, true);
	g_object_unref(self);
}
static char* conflict_reason(signing_item* item) {
	GString* reason = g_string_new(NULL);
	if (!item->output || !item->output[0]) {
		append_warning(reason, _("output unavailable"));
	}
	if (item->output_exists) {
		append_warning(reason, _("already exists"));
	}
	if (item->output_duplicate) {
		append_warning(reason, _("duplicated path"));
	}
	if (item->output_input_collision) {
		append_warning(reason, _("matches an input file"));
	}
	return g_string_free(reason, FALSE);
}
static void show_conflict_dialog(
    SignSigning* self,
    GArray*      indices,
    bool         hard
) {
	if (self->conflict_dialog) {
		gtk_window_present(GTK_WINDOW(self->conflict_dialog));
		return;
	}
	GString* body = g_string_new(
	    _("These output files need attention before signing:")
	);
	for (guint n = 0; n < indices->len; ++n) {
		guint         index = g_array_index(indices, guint, n);
		signing_item* item = index < self->items->len
		                         ? g_ptr_array_index(self->items, index)
		                         : NULL;
		if (!item) {
			continue;
		}
		char* display = item->output ? g_filename_display_name(item->output)
		                             : g_strdup(_("Unavailable"));
		char* reason = conflict_reason(item);
		g_string_append_printf(body, "\n• %s (%s)", display, reason);
		g_free(reason);
		g_free(display);
	}
	GtkWidget* window = gtk_window_new();
	self->conflict_dialog = window;
	g_object_add_weak_pointer(
	    G_OBJECT(window),
	    (gpointer*)&self->conflict_dialog
	);
	gtk_widget_set_name(window, "sign-conflict-dialog");
	gtk_window_set_title(GTK_WINDOW(window), _("Output files already exist"));
	gtk_window_set_modal(GTK_WINDOW(window), TRUE);
	gtk_window_set_transient_for(GTK_WINDOW(window), parent_window(self));
	gtk_window_set_destroy_with_parent(GTK_WINDOW(window), TRUE);
	gtk_window_set_default_size(GTK_WINDOW(window), 620, -1);
	if (parent_window(self)) {
		gtk_window_set_application(
		    GTK_WINDOW(window),
		    gtk_window_get_application(parent_window(self))
		);
	}
	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
	gtk_widget_set_margin_start(box, 24);
	gtk_widget_set_margin_end(box, 24);
	gtk_widget_set_margin_top(box, 24);
	gtk_widget_set_margin_bottom(box, 24);
	GtkWidget* label = gtk_label_new(body->str);
	g_string_free(body, TRUE);
	gtk_label_set_wrap(GTK_LABEL(label), TRUE);
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_label_set_selectable(GTK_LABEL(label), TRUE);
	gtk_box_append(GTK_BOX(box), label);
	GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	GtkWidget* cancel = gtk_button_new_with_label(_("Cancel"));
	gtk_widget_set_name(cancel, "sign-conflict-cancel");
	GtkWidget* names = gtk_button_new_with_label(
	    _("Choose a new location for each file")
	);
	gtk_widget_set_name(names, "sign-conflict-names");
	GtkWidget* folder = gtk_button_new_with_label(
	    _("Choose another folder for all files")
	);
	gtk_widget_set_name(folder, "sign-conflict-folder");
	GtkWidget* overwrite = gtk_button_new_with_label(_("Overwrite"));
	gtk_widget_set_name(overwrite, "sign-conflict-overwrite");
	gtk_widget_add_css_class(overwrite, "destructive-action");
	gtk_widget_set_sensitive(overwrite, !hard);
	if (hard) {
		gtk_widget_set_tooltip_text(
		    overwrite,
		    _("Duplicated or input paths cannot be overwritten")
		);
	}
	gtk_box_append(GTK_BOX(actions), cancel);
	gtk_box_append(GTK_BOX(actions), names);
	gtk_box_append(GTK_BOX(actions), folder);
	gtk_box_append(GTK_BOX(actions), overwrite);
	gtk_box_append(GTK_BOX(box), actions);
	conflict_request* request = g_new0(conflict_request, 1);
	request->self = g_object_ref(self);
	request->indices = copy_indices(indices);
	request->hard = hard;
	g_object_set_data_full(
	    G_OBJECT(window),
	    "sign-conflict-request",
	    request,
	    (GDestroyNotify)conflict_free
	);
	g_signal_connect(cancel, "clicked", G_CALLBACK(conflict_cancel), window);
	g_signal_connect(names, "clicked", G_CALLBACK(conflict_names), window);
	g_signal_connect(folder, "clicked", G_CALLBACK(conflict_folder), window);
	g_signal_connect(
	    overwrite,
	    "clicked",
	    G_CALLBACK(conflict_overwrite),
	    window
	);
	gtk_window_set_child(GTK_WINDOW(window), box);
	gtk_window_present(GTK_WINDOW(window));
}
void sign_signing_run(SignSigning* self) {
	if (self->busy || self->loading || !self->items->len || self->closing) {
		return;
	}
	if (self->conflict_dialog) {
		gtk_window_present(GTK_WINDOW(self->conflict_dialog));
		return;
	}
	bool    hard = refresh_output_warnings(self);
	GArray* conflicts = g_array_new(FALSE, FALSE, sizeof(guint));
	for (guint n = 0; n < self->items->len; ++n) {
		signing_item* item = g_ptr_array_index(self->items, n);
		if (!item->output || !item->output[0] || item->output_exists ||
		    item->output_duplicate || item->output_input_collision) {
			g_array_append_val(conflicts, n);
		}
	}
	if (conflicts->len) {
		show_conflict_dialog(self, conflicts, hard);
		g_array_unref(conflicts);
		return;
	}
	g_array_unref(conflicts);
	start_run(self, false);
}
static void start_run(SignSigning* self, bool overwrite) {
	self->busy = true;
	message(self, NULL);
	controls(self);
	signing_run* run = g_new0(signing_run, 1);
	run->overwrite = overwrite;
	run->items = g_ptr_array_new_with_free_func(item_unref);
	for (guint n = 0; n < self->items->len; ++n) {
		g_ptr_array_add(
		    run->items,
		    item_ref(g_ptr_array_index(self->items, n))
		);
	}
	run->profile = g_strdup(profile(self));
	run->self = self;
	run->cancellable = g_object_ref(self->lifetime);
	GtkWindow*      parent = parent_window(self);
	GtkApplication* app = parent ? gtk_window_get_application(parent) : NULL;
	if (app) {
		run->application = g_object_ref(G_APPLICATION(app));
		g_application_hold(run->application);
	}
	GTask* task = g_task_new(self, self->lifetime, run_finished, NULL);
	g_task_set_task_data(task, run, run_free);
	g_task_set_return_on_cancel(task, FALSE);
	g_task_run_in_thread(task, run_worker);
	g_object_unref(task);
}
static void sign_clicked(GtkButton* button, gpointer data) {
	(void)button;
	sign_signing_run(data);
}

static void signing_root(GtkWidget* widget) {
	GTK_WIDGET_CLASS(sign_signing_parent_class)->root(widget);
	SignSigning* self = SIGN_SIGNING(widget);
	self->closing = false;
	if (g_cancellable_is_cancelled(self->lifetime)) {
		g_object_unref(self->lifetime);
		self->lifetime = g_cancellable_new();
	}
}
static void signing_unroot(GtkWidget* widget) {
	SignSigning* self = SIGN_SIGNING(widget);
	self->closing = true;
	g_cancellable_cancel(self->lifetime);
	if (self->preview) {
		g_cancellable_cancel(self->preview);
	}
	if (self->pin_window) {
		gtk_window_destroy(self->pin_window);
	}
	if (self->conflict_dialog) {
		gtk_window_destroy(GTK_WINDOW(self->conflict_dialog));
	}
	GTK_WIDGET_CLASS(sign_signing_parent_class)->unroot(widget);
}
static void signing_dispose(GObject* object) {
	SignSigning* self = SIGN_SIGNING(object);
	g_cancellable_cancel(self->lifetime);
	if (self->preview) {
		g_cancellable_cancel(self->preview);
		g_object_unref(self->preview);
		self->preview = NULL;
	}
	if (self->profiles) {
		g_object_unref(self->profiles);
		self->profiles = NULL;
	}
	G_OBJECT_CLASS(sign_signing_parent_class)->dispose(object);
}
static void signing_finalize(GObject* object) {
	SignSigning* self = SIGN_SIGNING(object);
	g_ptr_array_unref(self->items);
	g_ptr_array_unref(self->profile_ids);
	g_object_unref(self->lifetime);
	if (self->surface) {
		cairo_surface_destroy(self->surface);
	}
	if (self->stamp) {
		cairo_surface_destroy(self->stamp);
	}
	G_OBJECT_CLASS(sign_signing_parent_class)->finalize(object);
}
static void sign_signing_class_init(SignSigningClass* klass) {
	signing_completed = g_signal_new(
	    "signing-completed",
	    G_TYPE_FROM_CLASS(klass),
	    G_SIGNAL_RUN_LAST,
	    0,
	    NULL,
	    NULL,
	    NULL,
	    G_TYPE_NONE,
	    1,
	    G_TYPE_POINTER
	);

	documents_signed = g_signal_new(
	    "documents-signed",
	    G_TYPE_FROM_CLASS(klass),
	    G_SIGNAL_RUN_LAST,
	    0,
	    NULL,
	    NULL,
	    NULL,
	    G_TYPE_NONE,
	    0
	);
	GTK_WIDGET_CLASS(klass)->root = signing_root;
	GTK_WIDGET_CLASS(klass)->unroot = signing_unroot;
	G_OBJECT_CLASS(klass)->dispose = signing_dispose;
	G_OBJECT_CLASS(klass)->finalize = signing_finalize;
}
static GtkWidget* icon_button(
    const char*  icon,
    const char*  tooltip,
    GCallback    callback,
    SignSigning* self
) {
	GtkWidget* button = gtk_button_new_from_icon_name(icon);
	gtk_widget_set_tooltip_text(button, tooltip);
	gtk_widget_add_css_class(button, "flat");
	g_signal_connect(button, "clicked", callback, self);
	return button;
}
static void sign_signing_init(SignSigning* self) {
	self->items = g_ptr_array_new_with_free_func(item_unref);
	self->profile_ids = g_ptr_array_new_with_free_func(g_free);
	self->lifetime = g_cancellable_new();
	self->page_number = 1;
	self->profiles = GTK_DROP_DOWN(
	    g_object_ref_sink(gtk_drop_down_new(NULL, NULL))
	);
	gtk_widget_set_name(GTK_WIDGET(self->profiles), "sign-profile");
	gtk_widget_set_tooltip_text(
	    GTK_WIDGET(self->profiles),
	    _("Signing profile")
	);
	GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
	g_signal_connect(factory, "setup", G_CALLBACK(profile_setup), NULL);
	g_signal_connect(factory, "bind", G_CALLBACK(profile_bind), NULL);
	gtk_drop_down_set_factory(self->profiles, factory);
	gtk_drop_down_set_list_factory(self->profiles, factory);
	g_object_unref(factory);
	g_signal_connect_object(
	    self->profiles,
	    "notify::selected",
	    G_CALLBACK(profile_changed),
	    self,
	    0
	);
	gtk_orientable_set_orientation(
	    GTK_ORIENTABLE(self),
	    GTK_ORIENTATION_VERTICAL
	);
	gtk_box_set_spacing(GTK_BOX(self), 12);
	gtk_widget_set_margin_start(GTK_WIDGET(self), 18);
	gtk_widget_set_margin_end(GTK_WIDGET(self), 18);
	gtk_widget_set_margin_top(GTK_WIDGET(self), 18);
	gtk_widget_set_margin_bottom(GTK_WIDGET(self), 18);
	self->filename = GTK_LABEL(gtk_label_new(NULL));
	gtk_label_set_ellipsize(self->filename, PANGO_ELLIPSIZE_MIDDLE);
	gtk_label_set_xalign(self->filename, 0);
	gtk_widget_set_hexpand(GTK_WIDGET(self->filename), TRUE);
	self->spinner = gtk_spinner_new();
	self->notice = GTK_LABEL(gtk_label_new(NULL));
	gtk_widget_set_name(GTK_WIDGET(self->notice), "sign-notice");
	gtk_label_set_wrap(self->notice, TRUE);
	gtk_label_set_xalign(self->notice, 0);
	gtk_label_set_selectable(self->notice, TRUE);
	gtk_widget_set_visible(GTK_WIDGET(self->notice), FALSE);
	gtk_box_append(GTK_BOX(self), GTK_WIDGET(self->notice));
	self->modes = GTK_STACK(gtk_stack_new());
	gtk_widget_set_vexpand(GTK_WIDGET(self->modes), TRUE);
	gtk_box_append(GTK_BOX(self), GTK_WIDGET(self->modes));
	GtkWidget* empty = gtk_button_new();
	gtk_widget_set_name(empty, "sign-choose");
	gtk_widget_add_css_class(empty, "flat");
	gtk_widget_add_css_class(empty, "validation-drop-area");
	gtk_widget_set_cursor_from_name(empty, "pointer");
	GtkWidget* empty_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_halign(empty_box, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(empty_box, GTK_ALIGN_CENTER);
	GtkWidget* icon = gtk_image_new_from_icon_name("document-edit-symbolic");
	gtk_image_set_pixel_size(GTK_IMAGE(icon), 48);
	gtk_box_append(GTK_BOX(empty_box), icon);
	GtkWidget* title = gtk_label_new(_("Drop files here to sign"));
	gtk_widget_add_css_class(title, "title-2");
	gtk_label_set_wrap(GTK_LABEL(title), TRUE);
	gtk_box_append(GTK_BOX(empty_box), title);
	GtkWidget* hint = gtk_label_new(_("Click to choose one or more files"));
	gtk_widget_add_css_class(hint, "dim-label");
	gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
	gtk_label_set_justify(GTK_LABEL(hint), GTK_JUSTIFY_CENTER);
	gtk_box_append(GTK_BOX(empty_box), hint);
	gtk_button_set_child(GTK_BUTTON(empty), empty_box);
	g_signal_connect(empty, "clicked", G_CALLBACK(choose_files), self);
	self->add = empty;
	gtk_widget_set_size_request(empty, -1, 150);
	gtk_widget_set_margin_top(empty_box, 24);
	gtk_widget_set_margin_bottom(empty_box, 24);
	gtk_box_prepend(GTK_BOX(self), empty);
	gtk_stack_add_named(
	    self->modes,
	    gtk_box_new(GTK_ORIENTATION_VERTICAL, 0),
	    "empty"
	);
	GtkWidget* queue = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	GtkWidget* queue_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	self->total = GTK_LABEL(gtk_label_new(NULL));
	gtk_widget_set_name(GTK_WIDGET(self->total), "sign-total");
	gtk_widget_add_css_class(GTK_WIDGET(self->total), "dim-label");
	gtk_label_set_xalign(self->total, 0);
	gtk_widget_set_hexpand(GTK_WIDGET(self->total), TRUE);
	gtk_box_append(GTK_BOX(queue_controls), GTK_WIDGET(self->total));
	self->clear = gtk_button_new_with_label(_("Clear all"));
	gtk_widget_set_name(self->clear, "sign-clear");
	g_signal_connect(self->clear, "clicked", G_CALLBACK(clear_clicked), self);
	gtk_box_append(GTK_BOX(queue_controls), self->clear);
	gtk_box_append(GTK_BOX(queue), queue_controls);
	GtkWidget* scroll = gtk_scrolled_window_new();
	gtk_widget_set_vexpand(scroll, TRUE);
	gtk_scrolled_window_set_policy(
	    GTK_SCROLLED_WINDOW(scroll),
	    GTK_POLICY_NEVER,
	    GTK_POLICY_AUTOMATIC
	);
	self->list = GTK_LIST_BOX(gtk_list_box_new());
	gtk_widget_set_valign(GTK_WIDGET(self->list), GTK_ALIGN_START);
	gtk_widget_set_name(GTK_WIDGET(self->list), "sign-documents");
	gtk_list_box_set_selection_mode(self->list, GTK_SELECTION_NONE);
	gtk_widget_add_css_class(GTK_WIDGET(self->list), "boxed-list");
	g_signal_connect(
	    self->list,
	    "row-activated",
	    G_CALLBACK(row_activated),
	    self
	);
	gtk_scrolled_window_set_child(
	    GTK_SCROLLED_WINDOW(scroll),
	    GTK_WIDGET(self->list)
	);
	gtk_box_append(GTK_BOX(queue), scroll);
	GtkWidget* footer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_widget_set_halign(actions, GTK_ALIGN_END);
	self->warning = gtk_image_new_from_icon_name("dialog-warning-symbolic");
	gtk_widget_set_name(self->warning, "sign-warning");
	gtk_widget_add_css_class(self->warning, "warning");
	gtk_box_append(GTK_BOX(actions), self->warning);
	self->sign_button = gtk_button_new_with_label(_("Sign"));
	gtk_widget_set_name(self->sign_button, "sign-run");
	gtk_widget_add_css_class(self->sign_button, "suggested-action");
	g_signal_connect(
	    self->sign_button,
	    "clicked",
	    G_CALLBACK(sign_clicked),
	    self
	);
	gtk_box_append(GTK_BOX(actions), self->sign_button);
	self->output_options = gtk_menu_button_new();
	gtk_widget_set_name(self->output_options, "sign-output-options");
	gtk_menu_button_set_icon_name(
	    GTK_MENU_BUTTON(self->output_options),
	    "view-more-symbolic"
	);
	gtk_widget_add_css_class(self->output_options, "flat");
	gtk_widget_set_valign(self->output_options, GTK_ALIGN_CENTER);
	gtk_widget_set_tooltip_text(self->output_options, _("Output file options"));
	GtkWidget* options = gtk_popover_new();
	GtkWidget* options_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_add_css_class(options_box, "sign-output-choices");
	gtk_widget_set_margin_start(options_box, 12);
	gtk_widget_set_margin_end(options_box, 12);
	gtk_widget_set_margin_top(options_box, 12);
	gtk_widget_set_margin_bottom(options_box, 12);
	GtkWidget* folder = gtk_button_new_with_label(
	    _("Choose output folder for all")
	);
	gtk_widget_set_name(folder, "sign-output-folder");
	gtk_widget_add_css_class(folder, "flat");
	GtkWidget* names = gtk_button_new_with_label(
	    _("Choose output individually")
	);
	gtk_widget_set_name(names, "sign-output-names");
	gtk_widget_add_css_class(names, "flat");
	gtk_label_set_xalign(
	    GTK_LABEL(gtk_button_get_child(GTK_BUTTON(folder))),
	    0
	);
	gtk_label_set_xalign(GTK_LABEL(gtk_button_get_child(GTK_BUTTON(names))), 0);
	g_signal_connect(
	    folder,
	    "clicked",
	    G_CALLBACK(output_folder_clicked),
	    self
	);
	g_signal_connect(names, "clicked", G_CALLBACK(output_names_clicked), self);
	gtk_box_append(GTK_BOX(options_box), folder);
	gtk_box_append(GTK_BOX(options_box), names);
	gtk_popover_set_child(GTK_POPOVER(options), options_box);
	gtk_menu_button_set_popover(GTK_MENU_BUTTON(self->output_options), options);
	gtk_box_append(GTK_BOX(actions), self->output_options);
	gtk_box_append(GTK_BOX(footer), actions);
	gtk_box_append(GTK_BOX(queue), footer);
	gtk_stack_add_named(self->modes, queue, "queue");
	GtkWidget* reader = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	GtkWidget* reader_tools = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	self->back = icon_button(
	    "go-previous-symbolic",
	    _("Document queue"),
	    G_CALLBACK(continue_clicked),
	    self
	);
	gtk_box_append(GTK_BOX(reader_tools), self->back);
	gtk_box_append(GTK_BOX(reader_tools), GTK_WIDGET(self->filename));
	gtk_box_append(GTK_BOX(reader_tools), self->spinner);
	self->image_button = icon_button(
	    "image-x-generic-symbolic",
	    _("Choose signature image"),
	    G_CALLBACK(choose_image),
	    self
	);
	gtk_widget_set_name(self->image_button, "sign-image");
	gtk_box_append(GTK_BOX(reader_tools), self->image_button);
	self->image_menu = gtk_menu_button_new();
	gtk_widget_set_name(self->image_menu, "sign-image-options");
	gtk_menu_button_set_icon_name(
	    GTK_MENU_BUTTON(self->image_menu),
	    "view-more-symbolic"
	);
	gtk_widget_add_css_class(self->image_menu, "flat");
	gtk_widget_set_tooltip_text(self->image_menu, _("Signature image options"));
	gtk_box_append(GTK_BOX(reader_tools), self->image_menu);
	self->default_button = icon_button(
	    "edit-undo-symbolic",
	    _("Use profile signature image"),
	    G_CALLBACK(reset_image),
	    self
	);
	gtk_widget_set_name(self->default_button, "sign-profile-image");
	gtk_box_append(GTK_BOX(reader_tools), self->default_button);
	gtk_box_append(GTK_BOX(reader), reader_tools);
	self->information = gtk_check_button_new_with_label(
	    _("Add signing information")
	);
	gtk_widget_set_name(self->information, "sign-information");
	gtk_widget_set_margin_start(self->information, 12);
	gtk_widget_set_margin_end(self->information, 12);
	gtk_widget_set_margin_top(self->information, 12);
	gtk_widget_set_margin_bottom(self->information, 12);
	gtk_check_button_set_active(GTK_CHECK_BUTTON(self->information), TRUE);
	g_signal_connect(
	    self->information,
	    "toggled",
	    G_CALLBACK(information_toggled),
	    self
	);
	GtkWidget* image_popover = gtk_popover_new();
	gtk_popover_set_child(GTK_POPOVER(image_popover), self->information);
	gtk_menu_button_set_popover(
	    GTK_MENU_BUTTON(self->image_menu),
	    image_popover
	);
	self->reader = SIGN_PDF_READER(sign_pdf_reader_new());
	gtk_widget_set_vexpand(GTK_WIDGET(self->reader), TRUE);
	self->canvas = sign_pdf_reader_canvas(self->reader);
	g_signal_connect(
	    self->reader,
	    "page-changed",
	    G_CALLBACK(reader_page),
	    self
	);
	g_signal_connect(
	    self->reader,
	    "placement-requested",
	    G_CALLBACK(reader_place),
	    self
	);
	g_signal_connect(
	    self->reader,
	    "remove-appearance",
	    G_CALLBACK(remove_appearance),
	    self
	);
	gtk_box_append(GTK_BOX(reader), GTK_WIDGET(self->reader));
	GtkWidget* bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_append(GTK_BOX(bottom), gtk_label_new(_("Page")));
	self->page = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(1, 1, 1));
	gtk_widget_set_name(GTK_WIDGET(self->page), "sign-page");
	gtk_spin_button_set_numeric(self->page, TRUE);
	g_signal_connect(
	    self->page,
	    "value-changed",
	    G_CALLBACK(page_changed),
	    self
	);
	gtk_box_append(GTK_BOX(bottom), GTK_WIDGET(self->page));
	self->page_count = GTK_LABEL(gtk_label_new(NULL));
	gtk_box_append(GTK_BOX(bottom), GTK_WIDGET(self->page_count));
	self->continue_button = gtk_button_new_with_label(
	    _("Continue without graphical signature")
	);
	gtk_widget_set_name(self->continue_button, "sign-continue");
	gtk_widget_set_hexpand(self->continue_button, TRUE);
	gtk_widget_add_css_class(self->continue_button, "suggested-action");
	gtk_label_set_wrap(
	    GTK_LABEL(gtk_button_get_child(GTK_BUTTON(self->continue_button))),
	    TRUE
	);
	g_signal_connect(
	    self->continue_button,
	    "clicked",
	    G_CALLBACK(continue_clicked),
	    self
	);
	gtk_box_append(GTK_BOX(bottom), self->continue_button);
	gtk_box_append(GTK_BOX(reader), bottom);
	gtk_stack_add_named(self->modes, reader, "reader");
	sign_file_drop_attach(
	    GTK_WIDGET(self),
	    files_dropped,
	    drop_cancel,
	    drop_state,
	    NULL
	);
	sign_signing_reload_profiles(self);
	controls(self);
}
GtkWidget* sign_signing_new(void) {
	return g_object_new(SIGN_TYPE_SIGNING, NULL);
}
