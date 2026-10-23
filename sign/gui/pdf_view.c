/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <math.h>
#include <poppler.h>
#include <qpdf/qpdf-c.h>
#include <sign/gui/error.h>
#include <sign/gui/i18n.h>
#include <sign/gui/pdf_view.h>
#include <stdint.h>
#include <string.h>
typedef struct {
	double width, height, unit;
} pdf_page;
struct sign_pdf_document {
	gint             references;
	PopplerDocument* document;
	GArray*          pages;
	GMutex           lock;
};
sign_pdf_document* sign_pdf_ref(sign_pdf_document* document) {
	if (document) {
		g_atomic_int_inc(&document->references);
	}
	return document;
}
void sign_pdf_unref(sign_pdf_document* document) {
	if (!document || !g_atomic_int_dec_and_test(&document->references)) {
		return;
	}
	g_object_unref(document->document);
	g_array_unref(document->pages);
	g_mutex_clear(&document->lock);
	g_free(document);
}
unsigned int sign_pdf_pages(const sign_pdf_document* document) {
	return document ? document->pages->len : 0;
}
bool sign_pdf_size(
    const sign_pdf_document* document,
    unsigned int             page,
    double*                  width,
    double*                  height
) {
	if (!document || !page || page > document->pages->len) {
		return false;
	}
	const pdf_page* p = &g_array_index(document->pages, pdf_page, page - 1);
	*width = p->width * p->unit;
	*height = p->height * p->unit;
	return true;
}
sign_pdf_document* sign_pdf_open(
    GFile*        file,
    GCancellable* cancellable,
    GError**      error
) {
	GFileInputStream* stream = g_file_read(file, cancellable, error);
	if (!stream) {
		return NULL;
	}
	char  magic[5];
	gsize length = 0;
	bool  read = g_input_stream_read_all(
        G_INPUT_STREAM(stream),
        magic,
        sizeof(magic),
        &length,
        cancellable,
        error
    );
	g_object_unref(stream);
	if (!read || length != sizeof(magic) || memcmp(magic, "%PDF-", 5)) {
		return NULL;
	}
	GBytes* bytes = g_file_load_bytes(file, cancellable, NULL, error);
	if (!bytes) {
		return NULL;
	}
	/* No PDF JavaScript, external links, forms or mutations are executed. */
	PopplerDocument* pdf = poppler_document_new_from_bytes(bytes, NULL, error);
	if (!pdf) {
		g_bytes_unref(bytes);
		return NULL;
	}
	sign_pdf_document* document = g_new0(sign_pdf_document, 1);
	document->references = 1;
	document->document = pdf;
	document->pages = g_array_new(FALSE, FALSE, sizeof(pdf_page));
	g_mutex_init(&document->lock);
	qpdf_data parser = qpdf_init();
	qpdf_silence_errors(parser);
	qpdf_set_suppress_warnings(parser, QPDF_TRUE);
	gsize       size;
	const char* data = g_bytes_get_data(bytes, &size);
	bool        ok = !(
        qpdf_read_memory(parser, "preview", data, size, NULL) & QPDF_ERRORS
    );
	int count = poppler_document_get_n_pages(pdf);
	ok = ok && count > 0 && qpdf_get_num_pages(parser) == count;
	for (int n = 0; ok && n < count; ++n) {
		PopplerPage* page = poppler_document_get_page(pdf, n);
		if (!page) {
			ok = false;
			break;
		}
		pdf_page p = {.unit = 1};
		poppler_page_get_size(page, &p.width, &p.height);
		g_object_unref(page);
		qpdf_oh object = qpdf_get_page_n(parser, (size_t)n);
		qpdf_oh unit = qpdf_oh_get_key(parser, object, "/UserUnit");
		if (!qpdf_oh_is_null(parser, unit)) {
			if (!qpdf_oh_is_number(parser, unit)) {
				ok = false;
				break;
			}
			p.unit = qpdf_oh_get_numeric_value(parser, unit);
		}
		ok = isfinite(p.width) && isfinite(p.height) && isfinite(p.unit) &&
		     p.width > 0 && p.height > 0 && p.unit > 0 && p.unit <= 75000 &&
		     isfinite(p.width * p.unit) && isfinite(p.height * p.unit) &&
		     p.width / p.height <= 10000 && p.height / p.width <= 10000;
		if (ok) {
			g_array_append_val(document->pages, p);
		}
	}
	qpdf_cleanup(&parser);
	g_bytes_unref(bytes);
	if (ok && !g_cancellable_set_error_if_cancelled(cancellable, error)) {
		return document;
	}
	sign_pdf_unref(document);
	if (!error || !*error) {
		g_set_error_literal(
		    error,
		    SIGN_GUI_ERROR,
		    G_IO_ERROR_INVALID_DATA,
		    _("Cannot read PDF page geometry")
		);
	}
	return NULL;
}
cairo_surface_t* sign_pdf_render(
    sign_pdf_document* document,
    unsigned int       page,
    GCancellable*      cancellable,
    GError**           error
) {
	if (g_cancellable_set_error_if_cancelled(cancellable, error)) {
		return NULL;
	}
	if (!page || page > document->pages->len) {
		g_set_error_literal(
		    error,
		    SIGN_GUI_ERROR,
		    G_IO_ERROR_INVALID_ARGUMENT,
		    _("Invalid page number")
		);
		return NULL;
	}
	pdf_page p = g_array_index(document->pages, pdf_page, page - 1);
	double   scale = fmin(1600.0 / p.width, 2400.0 / p.height);
	int      w = MAX(1, (int)ceil(p.width * scale)),
	    h = MAX(1, (int)ceil(p.height * scale));
	cairo_surface_t* surface = cairo_image_surface_create(
	    CAIRO_FORMAT_ARGB32,
	    w,
	    h
	);
	cairo_t* cr = cairo_create(surface);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_paint(cr);
	cairo_scale(cr, scale, scale);
	g_mutex_lock(&document->lock);
	PopplerPage* pdf_page = poppler_document_get_page(
	    document->document,
	    (int)page - 1
	);
	if (pdf_page) {
		poppler_page_render(pdf_page, cr);
		g_object_unref(pdf_page);
	}
	g_mutex_unlock(&document->lock);
	bool ok = pdf_page && cairo_status(cr) == CAIRO_STATUS_SUCCESS &&
	          cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS;
	cairo_destroy(cr);
	if (ok && !g_cancellable_set_error_if_cancelled(cancellable, error)) {
		return surface;
	}
	cairo_surface_destroy(surface);
	if (!error || !*error) {
		g_set_error_literal(
		    error,
		    SIGN_GUI_ERROR,
		    G_IO_ERROR_FAILED,
		    _("Cannot render this PDF page")
		);
	}
	return NULL;
}
bool sign_pdf_place(
    double                  page_width,
    double                  page_height,
    double                  ratio,
    double                  x,
    double                  y,
    double                  width,
    double                  height,
    libssig_pdf_appearance* result,
    GError**                error
) {
	if (!isfinite(page_width) || !isfinite(page_height) || !isfinite(ratio) ||
	    !isfinite(x) || !isfinite(y) || !isfinite(width) || !isfinite(height) ||
	    ratio <= 0 || x < 0 || y < 0 || width < 0 || height < 0) {
		goto invalid;
	}
	double low = fmax(
	    SIGN_APPEARANCE_MIN_SIZE,
	    SIGN_APPEARANCE_MIN_SIZE * ratio
	);
	double high = fmin(
	    SIGN_APPEARANCE_MAX_SIZE,
	    SIGN_APPEARANCE_MAX_SIZE * ratio
	);
	high = fmin(high, fmin(page_width - x, (page_height - y) * ratio));
	if (high < low) {
		goto invalid;
	}
	double desired = width > 0 || height > 0 ? fmin(width, height * ratio)
	                                         : SIGN_APPEARANCE_DEFAULT_WIDTH;
	result->x = x;
	result->y = y;
	result->width = fmax(low, fmin(high, desired));
	result->height = result->width / ratio;
	return true;
invalid:
	g_set_error_literal(
	    error,
	    SIGN_GUI_ERROR,
	    G_IO_ERROR_INVALID_ARGUMENT,
	    _("The signature image cannot fit at this position")
	);
	return false;
}
cairo_surface_t* sign_pdf_image(const libssig_pdf_appearance* image) {
	cairo_surface_t* surface = cairo_image_surface_create(
	    CAIRO_FORMAT_ARGB32,
	    (int)image->pixel_width,
	    (int)image->pixel_height
	);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(surface);
		return NULL;
	}
	unsigned char* dest = cairo_image_surface_get_data(surface);
	int            stride = cairo_image_surface_get_stride(surface);
	for (size_t y = 0; y < image->pixel_height; ++y) {
		for (size_t x = 0; x < image->pixel_width; ++x) {
			const unsigned char* p = image->pixels + y * image->row_stride +
			                         x * 4;
			uint32_t pixel = ((uint32_t)p[3] << 24) |
			                 ((uint32_t)((p[0] * p[3] + 127) / 255) << 16) |
			                 ((uint32_t)((p[1] * p[3] + 127) / 255) << 8) |
			                 (uint32_t)((p[2] * p[3] + 127) / 255);
			memcpy(dest + y * stride + x * 4, &pixel, sizeof(pixel));
		}
	}
	cairo_surface_mark_dirty(surface);
	return surface;
}
