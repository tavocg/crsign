/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <math.h>
#include <sign/gui/error.h>
#include <sign/gui/i18n.h>
#include <sign/gui/pdf_reader.h>

typedef struct {
	double width, height, x, y;
} page_box;
struct _SignPdfReader {
	GtkBox                 parent_instance;
	GtkScrolledWindow*     scroll;
	GtkWidget *            canvas, *remove, *plus, *minus;
	sign_pdf_document*     document;
	GArray*                pages;
	GHashTable*            surfaces;
	cairo_surface_t*       stamp;
	libssig_pdf_appearance appearance;
	GCancellable*          cancel;
	unsigned int           generation, page, jump;
	int                    viewport_width, viewport_height;
	double zoom, scale, max_width, max_height, content_height, content_width;
	double pointer_x, pointer_y, anchor_x, anchor_y, anchor_view_x,
	    anchor_view_y;
	unsigned int           anchor_page;
	bool                   pointer_known;
	bool                   pending, closing, layout, dragging, moving;
	libssig_pdf_appearance drag_appearance;
	unsigned int           drag_page;
	double                 drag_x, drag_y, drag_dx, drag_dy;
};
G_DEFINE_TYPE(SignPdfReader, sign_pdf_reader, GTK_TYPE_BOX)
enum { PAGE_CHANGED, PLACEMENT, REMOVE, N_SIGNALS };
static guint     signals[N_SIGNALS];
static void      request_page(SignPdfReader* self);
static page_box* box(SignPdfReader* self, unsigned int page) {
	return page && page <= self->pages->len
	           ? &g_array_index(self->pages, page_box, page - 1)
	           : NULL;
}
static void select_page(SignPdfReader* self, unsigned int page) {
	if (self->page != page) {
		self->page = page;
		g_signal_emit(self, signals[PAGE_CHANGED], 0, page);
	}
}
static void remove_position(SignPdfReader* self) {
	page_box* p = box(self, self->appearance.page_number);
	gtk_widget_set_visible(self->remove, self->stamp && p);
	if (self->stamp && p) {
		gtk_widget_set_margin_start(
		    self->remove,
		    MAX(0,
		        (int)round(
		            p->x +
		            (self->appearance.x + self->appearance.width) * self->scale
		        ) - 14)
		);
		gtk_widget_set_margin_top(
		    self->remove,
		    MAX(0, (int)round(p->y + self->appearance.y * self->scale) - 14)
		);
	}
}
/* Keep the document point beneath the mouse stable across a wheel zoom.
 * The adjustment bounds become available after GTK allocates the new canvas. */
static void apply_anchor(SignPdfReader* self) {
	page_box* p = box(self, self->anchor_page);
	if (!p || self->layout) {
		return;
	}
	GtkAdjustment* horizontal = gtk_scrolled_window_get_hadjustment(
	    self->scroll
	);
	GtkAdjustment* vertical = gtk_scrolled_window_get_vadjustment(self->scroll);
	if (gtk_adjustment_get_upper(horizontal) + 1 < self->content_width ||
	    gtk_adjustment_get_upper(vertical) + 1 < self->content_height) {
		return;
	}
	double x = p->x + self->anchor_x * self->scale - self->anchor_view_x,
	       y = p->y + self->anchor_y * self->scale - self->anchor_view_y;
	self->layout = true;
	self->anchor_page = 0;
	gtk_adjustment_set_value(
	    horizontal,
	    CLAMP(
	        x,
	        0,
	        MAX(0,
	            self->content_width - gtk_adjustment_get_page_size(horizontal))
	    )
	);
	gtk_adjustment_set_value(
	    vertical,
	    CLAMP(
	        y,
	        0,
	        MAX(0,
	            self->content_height - gtk_adjustment_get_page_size(vertical))
	    )
	);
	self->layout = false;
}
static void jump_pending(SignPdfReader* self) {
	page_box* p = box(self, self->jump);
	if (!p || self->layout) {
		return;
	}
	GtkAdjustment* a = gtk_scrolled_window_get_vadjustment(self->scroll);
	double         target = MAX(0, p->y - 16);
	if (gtk_adjustment_get_upper(a) + 1 < self->content_height) {
		return;
	}
	target = MIN(
	    target,
	    MAX(0, gtk_adjustment_get_upper(a) - gtk_adjustment_get_page_size(a))
	);
	unsigned int page = self->jump;
	self->jump = 0;
	gtk_adjustment_set_value(a, target);
	select_page(self, page);
}
static void scroll_changed(GtkAdjustment* adjustment, gpointer data) {
	(void)adjustment;
	SignPdfReader* self = data;
	if (self->layout || self->closing || !self->pages->len) {
		return;
	}
	apply_anchor(self);
	if (self->anchor_page) {
		return;
	}
	jump_pending(self);
	if (!self->jump) {
		GtkAdjustment* a = gtk_scrolled_window_get_vadjustment(self->scroll);
		double         maximum = gtk_adjustment_get_upper(a) -
		                 gtk_adjustment_get_page_size(a);
		if (maximum <= 0) {
			request_page(self);
			return;
		}
		if (gtk_adjustment_get_value(a) >= maximum - 1) {
			select_page(self, self->pages->len);
			request_page(self);
			gtk_widget_queue_draw(self->canvas);
			return;
		}
		double center = gtk_adjustment_get_value(a) +
		                gtk_adjustment_get_page_size(a) / 2;
		for (guint n = 0; n < self->pages->len; ++n) {
			page_box* p = box(self, n + 1);
			if (center < p->y + p->height * self->scale + 8 ||
			    n + 1 == self->pages->len) {
				select_page(self, n + 1);
				break;
			}
		}
	}
	request_page(self);
	gtk_widget_queue_draw(self->canvas);
}
static void layout_pages(SignPdfReader* self) {
	if (!self->document || self->viewport_width <= 32 ||
	    self->viewport_height <= 32) {
		return;
	}
	self->layout = true;
	self->scale = MIN((self->viewport_width - 32) / self->max_width,
	                  (self->viewport_height - 32) / self->max_height) *
	              self->zoom;
	double width = MAX(
	           self->viewport_width,
	           ceil(self->max_width * self->scale) + 32
	       ),
	       y = 16;
	for (guint n = 0; n < self->pages->len; ++n) {
		page_box* p = box(self, n + 1);
		p->x = (width - p->width * self->scale) / 2;
		p->y = y;
		y += p->height * self->scale + 16;
	}
	self->content_height = MIN(ceil(y), G_MAXINT / 2);
	self->content_width = MIN(width, G_MAXINT / 2);
	gtk_drawing_area_set_content_width(
	    GTK_DRAWING_AREA(self->canvas),
	    (int)MIN(width, G_MAXINT / 2)
	);
	gtk_drawing_area_set_content_height(
	    GTK_DRAWING_AREA(self->canvas),
	    (int)MIN(ceil(y), G_MAXINT / 2)
	);
	self->layout = false;
	remove_position(self);
	apply_anchor(self);
	jump_pending(self);
	gtk_widget_queue_draw(self->canvas);
	request_page(self);
}
static gboolean tick(GtkWidget* widget, GdkFrameClock* clock, gpointer data) {
	(void)widget;
	(void)clock;
	SignPdfReader* self = data;
	int            w = gtk_widget_get_width(GTK_WIDGET(self->scroll)),
	    h = gtk_widget_get_height(GTK_WIDGET(self->scroll));
	if (w != self->viewport_width || h != self->viewport_height) {
		self->viewport_width = w;
		self->viewport_height = h;
		if (self->document) {
			if (!self->anchor_page) {
				self->jump = self->page;
			}
			layout_pages(self);
		}
	}
	return G_SOURCE_CONTINUE;
}
static bool visible(SignPdfReader* self, unsigned int page) {
	page_box*      p = box(self, page);
	GtkAdjustment* a = gtk_scrolled_window_get_vadjustment(self->scroll);
	double         top = gtk_adjustment_get_value(a),
	       bottom = top + gtk_adjustment_get_page_size(a);
	return p && p->y <= bottom && p->y + p->height * self->scale >= top;
}
typedef struct {
	sign_pdf_document* document;
	unsigned int       page, generation;
	cairo_surface_t*   surface;
} render_job;
static void render_free(gpointer data) {
	render_job* job = data;
	sign_pdf_unref(job->document);
	if (job->surface) {
		cairo_surface_destroy(job->surface);
	}
	g_free(job);
}
static void render_worker(
    GTask*        task,
    gpointer      source,
    gpointer      data,
    GCancellable* cancel
) {
	(void)source;
	render_job* job = data;
	GError*     error = NULL;
	job->surface = sign_pdf_render(job->document, job->page, cancel, &error);
	if (job->surface) {
		g_task_return_boolean(task, TRUE);
	} else {
		g_task_return_error(task, error);
	}
}
static void render_finished(
    GObject*      source,
    GAsyncResult* result,
    gpointer      data
) {
	(void)data;
	SignPdfReader* self = SIGN_PDF_READER(source);
	render_job*    job = g_task_get_task_data(G_TASK(result));
	GError*        error = NULL;
	bool           ok = g_task_propagate_boolean(G_TASK(result), &error);
	self->pending = false;
	if (!self->closing && job->generation == self->generation) {
		if (ok) {
			g_hash_table_replace(
			    self->surfaces,
			    GUINT_TO_POINTER(job->page),
			    job->surface
			);
			job->surface = NULL;
			/* Bound the cache while retaining all pages currently on screen. */
			GHashTableIter iter;
			gpointer       key;
			g_hash_table_iter_init(&iter, self->surfaces);
			while (g_hash_table_size(self->surfaces) > 8 &&
			       g_hash_table_iter_next(&iter, &key, NULL)) {
				if (!visible(self, GPOINTER_TO_UINT(key))) {
					g_hash_table_iter_remove(&iter);
				}
			}
		} else if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			/* A failed page is cached as an error surface to prevent retries.
			 */
			cairo_surface_t* failed = cairo_image_surface_create(
			    CAIRO_FORMAT_ARGB32,
			    1,
			    1
			);
			g_hash_table_replace(
			    self->surfaces,
			    GUINT_TO_POINTER(job->page),
			    failed
			);
			gtk_widget_set_tooltip_text(
			    self->canvas,
			    _("Cannot render this PDF page")
			);
		}
		gtk_widget_queue_draw(self->canvas);
	}
	g_clear_error(&error);
	if (!self->closing) {
		request_page(self);
	}
}
static void request_page(SignPdfReader* self) {
	if (self->pending || self->closing || !self->document || self->scale <= 0) {
		return;
	}
	for (guint n = 1; n <= self->pages->len; ++n) {
		if (!visible(self, n) ||
		    g_hash_table_contains(self->surfaces, GUINT_TO_POINTER(n))) {
			continue;
		}
		render_job* job = g_new0(render_job, 1);
		job->document = sign_pdf_ref(self->document);
		job->page = n;
		job->generation = self->generation;
		self->pending = true;
		GTask* task = g_task_new(self, self->cancel, render_finished, NULL);
		g_task_set_task_data(task, job, render_free);
		g_task_run_in_thread(task, render_worker);
		g_object_unref(task);
		break;
	}
}
static void paint(
    cairo_t*         cr,
    cairo_surface_t* surface,
    double           x,
    double           y,
    double           w,
    double           h
) {
	cairo_save(cr);
	cairo_translate(cr, x, y);
	cairo_scale(
	    cr,
	    w / cairo_image_surface_get_width(surface),
	    h / cairo_image_surface_get_height(surface)
	);
	cairo_set_source_surface(cr, surface, 0, 0);
	cairo_paint(cr);
	cairo_restore(cr);
}
static void draw(
    GtkDrawingArea* area,
    cairo_t*        cr,
    int             width,
    int             height,
    gpointer        data
) {
	(void)area;
	(void)width;
	(void)height;
	SignPdfReader* self = data;
	if (self->scale <= 0) {
		return;
	}
	for (guint n = 1; n <= self->pages->len; ++n) {
		if (!visible(self, n)) {
			continue;
		}
		page_box* p = box(self, n);
		cairo_set_source_rgb(cr, 1, 1, 1);
		cairo_rectangle(
		    cr,
		    p->x,
		    p->y,
		    p->width * self->scale,
		    p->height * self->scale
		);
		cairo_fill(cr);
		cairo_surface_t* surface = g_hash_table_lookup(
		    self->surfaces,
		    GUINT_TO_POINTER(n)
		);
		if (surface) {
			paint(
			    cr,
			    surface,
			    p->x,
			    p->y,
			    p->width * self->scale,
			    p->height * self->scale
			);
		}
		if (self->stamp && self->appearance.page_number == n) {
			const libssig_pdf_appearance* a = &self->appearance;
			double x = p->x + a->x * self->scale, y = p->y + a->y * self->scale,
			       w = a->width * self->scale, h = a->height * self->scale;
			paint(cr, self->stamp, x, y, w, h);
			cairo_set_source_rgb(cr, .15, .45, .85);
			cairo_set_line_width(cr, 2);
			cairo_rectangle(cr, x, y, w, h);
			cairo_stroke(cr);
		}
	}
	if (self->dragging && !self->moving) {
		cairo_rectangle(
		    cr,
		    self->drag_x + MIN(0, self->drag_dx),
		    self->drag_y + MIN(0, self->drag_dy),
		    fabs(self->drag_dx),
		    fabs(self->drag_dy)
		);
		cairo_set_source_rgba(cr, .2, .5, .9, .2);
		cairo_fill_preserve(cr);
		cairo_set_source_rgb(cr, .15, .45, .85);
		cairo_set_line_width(cr, 2);
		cairo_stroke(cr);
	}
}
static void drag_begin(
    GtkGestureDrag* gesture,
    double          x,
    double          y,
    gpointer        data
) {
	(void)gesture;
	SignPdfReader* self = data;
	if (!self->stamp || self->scale <= 0) {
		return;
	}
	for (guint n = 1; n <= self->pages->len; ++n) {
		page_box* p = box(self, n);
		if (x < p->x || x > p->x + p->width * self->scale || y < p->y ||
		    y > p->y + p->height * self->scale) {
			continue;
		}
		select_page(self, n);
		double px = (x - p->x) / self->scale, py = (y - p->y) / self->scale;
		const libssig_pdf_appearance* a = &self->appearance;
		self->moving = a->page_number == n && px >= a->x &&
		               px <= a->x + a->width && py >= a->y &&
		               py <= a->y + a->height;
		self->drag_appearance = self->appearance;
		self->dragging = true;
		self->drag_page = n;
		self->drag_x = x;
		self->drag_y = y;
		self->drag_dx = self->drag_dy = 0;
		return;
	}
}
static void drag_update(
    GtkGestureDrag* gesture,
    double          dx,
    double          dy,
    gpointer        data
) {
	(void)gesture;
	SignPdfReader* self = data;
	if (self->dragging) {
		self->drag_dx = dx;
		self->drag_dy = dy;
		if (self->moving) {
			page_box* p = box(self, self->drag_page);
			self->appearance.x = CLAMP(
			    self->drag_appearance.x + dx / self->scale,
			    0,
			    p->width - self->drag_appearance.width
			);
			self->appearance.y = CLAMP(
			    self->drag_appearance.y + dy / self->scale,
			    0,
			    p->height - self->drag_appearance.height
			);
			remove_position(self);
		}
		gtk_widget_queue_draw(self->canvas);
	}
}
static void drag_end(
    GtkGestureDrag* gesture,
    double          dx,
    double          dy,
    gpointer        data
) {
	(void)gesture;
	SignPdfReader* self = data;
	if (!self->dragging) {
		return;
	}
	self->dragging = false;
	page_box* p = box(self, self->drag_page);
	if (!p) {
		return;
	}
	double x = (self->drag_x - p->x) / self->scale,
	       y = (self->drag_y - p->y) / self->scale, w = 0, h = 0;
	if (self->moving) {
		x = CLAMP(
		    self->drag_appearance.x + dx / self->scale,
		    0,
		    p->width - self->drag_appearance.width
		);
		y = CLAMP(
		    self->drag_appearance.y + dy / self->scale,
		    0,
		    p->height - self->drag_appearance.height
		);
		w = self->drag_appearance.width;
		h = self->drag_appearance.height;
		self->moving = false;
	} else if (hypot(dx, dy) >= 4) {
		double end_x = CLAMP(x + dx / self->scale, 0, p->width),
		       end_y = CLAMP(y + dy / self->scale, 0, p->height);
		w = fabs(end_x - x);
		h = fabs(end_y - y);
		x = MIN(x, end_x);
		y = MIN(y, end_y);
	}
	g_signal_emit(self, signals[PLACEMENT], 0, self->drag_page, x, y, w, h);
	gtk_widget_queue_draw(self->canvas);
}
static void drag_cancel(
    GtkGesture*       gesture,
    GdkEventSequence* sequence,
    gpointer          data
) {
	(void)gesture;
	(void)sequence;
	SignPdfReader* self = data;
	if (self->dragging && self->moving) {
		self->appearance = self->drag_appearance;
	}
	self->dragging = self->moving = false;
	remove_position(self);
	gtk_widget_queue_draw(self->canvas);
}
static void remove_clicked(GtkButton* button, gpointer data) {
	(void)button;
	g_signal_emit(data, signals[REMOVE], 0);
}
static void zoom_clicked(GtkButton* button, gpointer data) {
	SignPdfReader* self = data;
	if (self->dragging) {
		drag_cancel(NULL, NULL, self);
	}
	self->zoom = CLAMP(
	    self->zoom * (GTK_WIDGET(button) == self->plus ? 1.25 : .8),
	    .25,
	    8
	);
	self->anchor_page = 0;
	self->jump = self->page;
	layout_pages(self);
	gtk_widget_set_sensitive(self->plus, self->zoom < 8);
	gtk_widget_set_sensitive(self->minus, self->zoom > .25);
}
static gboolean zoom_key_in(GtkWidget* widget, GVariant* args, gpointer data) {
	(void)widget;
	(void)args;
	SignPdfReader* self = data;
	if (!gtk_widget_get_mapped(GTK_WIDGET(self))) {
		return FALSE;
	}
	zoom_clicked(GTK_BUTTON(self->plus), self);
	return TRUE;
}
static gboolean zoom_key_out(GtkWidget* widget, GVariant* args, gpointer data) {
	(void)widget;
	(void)args;
	SignPdfReader* self = data;
	if (!gtk_widget_get_mapped(GTK_WIDGET(self))) {
		return FALSE;
	}
	zoom_clicked(GTK_BUTTON(self->minus), self);
	return TRUE;
}
static void pointer_motion(
    GtkEventControllerMotion* controller,
    double                    x,
    double                    y,
    gpointer                  data
) {
	(void)controller;
	SignPdfReader* self = data;
	self->pointer_x = x;
	self->pointer_y = y;
	self->pointer_known = true;
}
static gboolean zoom_scroll(
    GtkEventControllerScroll* controller,
    double                    dx,
    double                    dy,
    gpointer                  data
) {
	(void)dx;
	SignPdfReader* self = data;
	if (!(gtk_event_controller_get_current_event_state(
	          GTK_EVENT_CONTROLLER(controller)
	      ) &
	      GDK_CONTROL_MASK) ||
	    dy == 0) {
		return FALSE;
	}
	if (!self->document || self->dragging) {
		return TRUE;
	}
	graphene_point_t from = GRAPHENE_POINT_INIT(
	                     self->pointer_x,
	                     self->pointer_y
	                 ),
	                 view;
	if (!self->pointer_known || !gtk_widget_compute_point(
	                                GTK_WIDGET(self),
	                                GTK_WIDGET(self->scroll),
	                                &from,
	                                &view
	                            )) {
		zoom_clicked(GTK_BUTTON(dy < 0 ? self->plus : self->minus), self);
		return TRUE;
	}
	GtkAdjustment* horizontal = gtk_scrolled_window_get_hadjustment(
	    self->scroll
	);
	GtkAdjustment* vertical = gtk_scrolled_window_get_vadjustment(self->scroll);
	double         x = gtk_adjustment_get_value(horizontal) + view.x,
	       y = gtk_adjustment_get_value(vertical) + view.y;
	unsigned int page = self->page;
	for (guint n = 1; n <= self->pages->len; ++n) {
		page_box* p = box(self, n);
		if (y <= p->y + p->height * self->scale || n == self->pages->len) {
			page = n;
			break;
		}
	}
	page_box* p = box(self, page);
	if (!p) {
		return TRUE;
	}
	if (!self->anchor_page || fabs(view.x - self->anchor_view_x) > 1 ||
	    fabs(view.y - self->anchor_view_y) > 1) {
		self->anchor_page = page;
		self->anchor_x = (x - p->x) / self->scale;
		self->anchor_y = (y - p->y) / self->scale;
		self->anchor_view_x = view.x;
		self->anchor_view_y = view.y;
	}
	self->jump = 0;
	self->zoom = CLAMP(self->zoom * (dy < 0 ? 1.25 : .8), .25, 8);
	layout_pages(self);
	gtk_widget_set_sensitive(self->plus, self->zoom < 8);
	gtk_widget_set_sensitive(self->minus, self->zoom > .25);
	return TRUE;
}
bool sign_pdf_reader_page_bounds(
    SignPdfReader*   self,
    unsigned int     page,
    graphene_rect_t* bounds
) {
	page_box* p = box(self, page);
	if (!p || self->scale <= 0) {
		return false;
	}
	graphene_rect_init(
	    bounds,
	    (float)p->x,
	    (float)p->y,
	    (float)(p->width * self->scale),
	    (float)(p->height * self->scale)
	);
	return true;
}
void sign_pdf_reader_set_document(
    SignPdfReader*     self,
    sign_pdf_document* document,
    unsigned int       page,
    cairo_surface_t*   surface
) {
	bool changed = self->document != document;
	if (changed) {
		self->anchor_page = 0;
		++self->generation;
		sign_pdf_unref(self->document);
		self->document = sign_pdf_ref(document);
		g_array_set_size(self->pages, 0);
		g_hash_table_remove_all(self->surfaces);
		self->max_width = self->max_height = 0;
		for (guint n = 1; n <= sign_pdf_pages(document); ++n) {
			page_box p = {0};
			sign_pdf_size(document, n, &p.width, &p.height);
			self->max_width = MAX(self->max_width, p.width);
			self->max_height = MAX(self->max_height, p.height);
			g_array_append_val(self->pages, p);
		}
		self->zoom = 1;
		self->page = page;
		self->jump = page;
		gtk_widget_set_sensitive(self->plus, TRUE);
		gtk_widget_set_sensitive(self->minus, TRUE);
	}
	if (surface) {
		g_hash_table_replace(
		    self->surfaces,
		    GUINT_TO_POINTER(page),
		    cairo_surface_reference(surface)
		);
	}
	if (changed) {
		layout_pages(self);
	}
}
void sign_pdf_reader_set_stamp(
    SignPdfReader*                self,
    cairo_surface_t*              stamp,
    const libssig_pdf_appearance* appearance
) {
	if (self->stamp) {
		cairo_surface_destroy(self->stamp);
	}
	self->stamp = stamp ? cairo_surface_reference(stamp) : NULL;
	self->dragging = self->moving = false;
	self->appearance = appearance ? *appearance : (libssig_pdf_appearance){0};
	remove_position(self);
	gtk_widget_queue_draw(self->canvas);
}
void sign_pdf_reader_goto(SignPdfReader* self, unsigned int page) {
	if (!box(self, page)) {
		return;
	}
	self->anchor_page = 0;
	self->jump = page;
	jump_pending(self);
}
GtkWidget* sign_pdf_reader_canvas(SignPdfReader* self) {
	return self->canvas;
}
static void root(GtkWidget* widget) {
	GTK_WIDGET_CLASS(sign_pdf_reader_parent_class)->root(widget);
	SignPdfReader* self = SIGN_PDF_READER(widget);
	self->closing = false;
	if (g_cancellable_is_cancelled(self->cancel)) {
		g_object_unref(self->cancel);
		self->cancel = g_cancellable_new();
	}
}
static void unroot(GtkWidget* widget) {
	SignPdfReader* self = SIGN_PDF_READER(widget);
	self->closing = true;
	g_cancellable_cancel(self->cancel);
	GTK_WIDGET_CLASS(sign_pdf_reader_parent_class)->unroot(widget);
}
static void finalize(GObject* object) {
	SignPdfReader* self = SIGN_PDF_READER(object);
	sign_pdf_unref(self->document);
	g_array_unref(self->pages);
	g_hash_table_unref(self->surfaces);
	g_object_unref(self->cancel);
	if (self->stamp) {
		cairo_surface_destroy(self->stamp);
	}
	G_OBJECT_CLASS(sign_pdf_reader_parent_class)->finalize(object);
}
static void sign_pdf_reader_class_init(SignPdfReaderClass* klass) {
	G_OBJECT_CLASS(klass)->finalize = finalize;
	GTK_WIDGET_CLASS(klass)->root = root;
	GTK_WIDGET_CLASS(klass)->unroot = unroot;
	signals[PAGE_CHANGED] = g_signal_new(
	    "page-changed",
	    G_TYPE_FROM_CLASS(klass),
	    G_SIGNAL_RUN_LAST,
	    0,
	    NULL,
	    NULL,
	    NULL,
	    G_TYPE_NONE,
	    1,
	    G_TYPE_UINT
	);
	signals[PLACEMENT] = g_signal_new(
	    "placement-requested",
	    G_TYPE_FROM_CLASS(klass),
	    G_SIGNAL_RUN_LAST,
	    0,
	    NULL,
	    NULL,
	    NULL,
	    G_TYPE_NONE,
	    5,
	    G_TYPE_UINT,
	    G_TYPE_DOUBLE,
	    G_TYPE_DOUBLE,
	    G_TYPE_DOUBLE,
	    G_TYPE_DOUBLE
	);
	signals[REMOVE] = g_signal_new(
	    "remove-appearance",
	    G_TYPE_FROM_CLASS(klass),
	    G_SIGNAL_RUN_LAST,
	    0,
	    NULL,
	    NULL,
	    NULL,
	    G_TYPE_NONE,
	    0
	);
}
static void draw_cross(
    GtkDrawingArea* area,
    cairo_t*        cr,
    int             width,
    int             height,
    gpointer        data
) {
	(void)area;
	(void)data;
	double size = MIN(width, height) * .4, x = (width - size) / 2,
	       y = (height - size) / 2;
	cairo_set_source_rgb(cr, .12, .12, .12);
	cairo_set_line_width(cr, 1.8);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_move_to(cr, x, y);
	cairo_line_to(cr, x + size, y + size);
	cairo_move_to(cr, x + size, y);
	cairo_line_to(cr, x, y + size);
	cairo_stroke(cr);
}
static void sign_pdf_reader_init(SignPdfReader* self) {
	self->zoom = 1;
	self->scale = 1;
	self->pages = g_array_new(FALSE, FALSE, sizeof(page_box));
	self->surfaces = g_hash_table_new_full(
	    g_direct_hash,
	    g_direct_equal,
	    NULL,
	    (GDestroyNotify)cairo_surface_destroy
	);
	self->cancel = g_cancellable_new();
	GtkWidget* outer = gtk_overlay_new();
	gtk_widget_set_vexpand(outer, TRUE);
	gtk_widget_set_hexpand(outer, TRUE);
	gtk_box_append(GTK_BOX(self), outer);
	self->scroll = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new());
	gtk_widget_set_name(GTK_WIDGET(self->scroll), "sign-pdf-scroll");
	gtk_scrolled_window_set_policy(
	    self->scroll,
	    GTK_POLICY_AUTOMATIC,
	    GTK_POLICY_AUTOMATIC
	);
	gtk_overlay_set_child(GTK_OVERLAY(outer), GTK_WIDGET(self->scroll));
	GtkWidget* overlay = gtk_overlay_new();
	self->canvas = gtk_drawing_area_new();
	gtk_widget_set_name(self->canvas, "sign-pdf-canvas");
	gtk_widget_set_cursor_from_name(self->canvas, "crosshair");
	gtk_widget_set_tooltip_text(
	    self->canvas,
	    _("Click or drag to place the graphical signature")
	);
	gtk_drawing_area_set_draw_func(
	    GTK_DRAWING_AREA(self->canvas),
	    draw,
	    self,
	    NULL
	);
	GtkGesture* drag = gtk_gesture_drag_new();
	g_signal_connect(drag, "drag-begin", G_CALLBACK(drag_begin), self);
	g_signal_connect(drag, "drag-update", G_CALLBACK(drag_update), self);
	g_signal_connect(drag, "drag-end", G_CALLBACK(drag_end), self);
	g_signal_connect(drag, "cancel", G_CALLBACK(drag_cancel), self);
	gtk_widget_add_controller(self->canvas, GTK_EVENT_CONTROLLER(drag));
	gtk_overlay_set_child(GTK_OVERLAY(overlay), self->canvas);
	self->remove = gtk_button_new();
	GtkWidget* cross = gtk_drawing_area_new();
	gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(cross), 16);
	gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(cross), 16);
	gtk_drawing_area_set_draw_func(
	    GTK_DRAWING_AREA(cross),
	    draw_cross,
	    NULL,
	    NULL
	);
	gtk_button_set_child(GTK_BUTTON(self->remove), cross);
	gtk_widget_set_name(self->remove, "sign-remove-appearance");
	gtk_widget_set_tooltip_text(self->remove, _("Remove graphical signature"));
	gtk_widget_add_css_class(self->remove, "signature-remove");
	gtk_widget_set_halign(self->remove, GTK_ALIGN_START);
	gtk_widget_set_valign(self->remove, GTK_ALIGN_START);
	gtk_widget_set_visible(self->remove, FALSE);
	g_signal_connect(self->remove, "clicked", G_CALLBACK(remove_clicked), self);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), self->remove);
	gtk_scrolled_window_set_child(self->scroll, overlay);
	GtkWidget* zoom = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_set_halign(zoom, GTK_ALIGN_END);
	gtk_widget_set_valign(zoom, GTK_ALIGN_END);
	gtk_widget_set_margin_end(zoom, 16);
	gtk_widget_set_margin_bottom(zoom, 16);
	self->plus = gtk_button_new_with_label("+");
	self->minus = gtk_button_new_with_label("−");
	gtk_widget_set_name(self->plus, "sign-zoom-in");
	gtk_widget_set_name(self->minus, "sign-zoom-out");
	gtk_widget_set_tooltip_text(self->plus, _("Zoom in"));
	gtk_widget_set_tooltip_text(self->minus, _("Zoom out"));
	gtk_widget_add_css_class(self->plus, "circular");
	gtk_widget_add_css_class(self->minus, "circular");
	gtk_widget_add_css_class(self->plus, "osd");
	gtk_widget_add_css_class(self->minus, "osd");
	g_signal_connect(self->plus, "clicked", G_CALLBACK(zoom_clicked), self);
	g_signal_connect(self->minus, "clicked", G_CALLBACK(zoom_clicked), self);
	gtk_box_append(GTK_BOX(zoom), self->plus);
	gtk_box_append(GTK_BOX(zoom), self->minus);
	gtk_overlay_add_overlay(GTK_OVERLAY(outer), zoom);
	GtkAdjustment* a = gtk_scrolled_window_get_vadjustment(self->scroll);
	g_signal_connect(a, "value-changed", G_CALLBACK(scroll_changed), self);
	g_signal_connect(a, "changed", G_CALLBACK(scroll_changed), self);
	GtkShortcutController* keys = GTK_SHORTCUT_CONTROLLER(
	    gtk_shortcut_controller_new()
	);
	gtk_shortcut_controller_set_scope(keys, GTK_SHORTCUT_SCOPE_GLOBAL);
	const guint keyvals[] = {
	    GDK_KEY_plus,
	    GDK_KEY_equal,
	    GDK_KEY_KP_Add,
	    GDK_KEY_minus,
	    GDK_KEY_KP_Subtract
	};
	for (size_t n = 0; n < G_N_ELEMENTS(keyvals); ++n) {
		GtkShortcutTrigger* trigger = gtk_keyval_trigger_new(
		    keyvals[n],
		    GDK_CONTROL_MASK
		);
		GtkShortcutAction* action = gtk_callback_action_new(
		    n < 3 ? zoom_key_in : zoom_key_out,
		    self,
		    NULL
		);
		gtk_shortcut_controller_add_shortcut(
		    keys,
		    gtk_shortcut_new(trigger, action)
		);
	}
	/* Ctrl+Shift+'=' produces '+' on several common keyboard layouts. */
	gtk_shortcut_controller_add_shortcut(
	    keys,
	    gtk_shortcut_new(
	        gtk_keyval_trigger_new(
	            GDK_KEY_plus,
	            GDK_CONTROL_MASK | GDK_SHIFT_MASK
	        ),
	        gtk_callback_action_new(zoom_key_in, self, NULL)
	    )
	);
	gtk_widget_add_controller(GTK_WIDGET(self), GTK_EVENT_CONTROLLER(keys));
	GtkEventController* wheel = gtk_event_controller_scroll_new(
	    GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES
	);
	gtk_event_controller_set_propagation_phase(wheel, GTK_PHASE_CAPTURE);
	g_signal_connect(wheel, "scroll", G_CALLBACK(zoom_scroll), self);
	gtk_widget_add_controller(GTK_WIDGET(self), wheel);
	GtkEventController* motion = gtk_event_controller_motion_new();
	gtk_event_controller_set_propagation_phase(motion, GTK_PHASE_CAPTURE);
	g_signal_connect(motion, "motion", G_CALLBACK(pointer_motion), self);
	g_signal_connect(motion, "enter", G_CALLBACK(pointer_motion), self);
	gtk_widget_add_controller(GTK_WIDGET(self), motion);
	g_signal_connect(
	    gtk_scrolled_window_get_hadjustment(self->scroll),
	    "changed",
	    G_CALLBACK(scroll_changed),
	    self
	);
	gtk_widget_add_tick_callback(GTK_WIDGET(self), tick, self, NULL);
}
GtkWidget* sign_pdf_reader_new(void) {
	return g_object_new(SIGN_TYPE_PDF_READER, NULL);
}
