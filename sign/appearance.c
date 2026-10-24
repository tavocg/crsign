/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <librsvg/rsvg.h>
#include <limits.h>
#include <math.h>
#include <openssl/x509.h>
#include <pango/pangocairo.h>
#include <sign/appearance.h>
#include <sign/io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define _(message) g_dgettext("crsign", message)

/* No base URI is supplied: user SVGs cannot load neighbouring files. */
char* appearance_default_svg(const libssig_certificate* cert) {
	if (!cert || !cert->der || !cert->der_len || cert->der_len > LONG_MAX) {
		return NULL;
	}
	const unsigned char* der = cert->der;
	X509*                x509 = d2i_X509(NULL, &der, (long)cert->der_len);
	if (!x509) {
		return NULL;
	}
	X509_NAME*     subject = X509_get_subject_name(x509);
	unsigned char* name = NULL;
	int            length = -1;
	const int      fields[] = {NID_commonName, NID_organizationName};
	for (size_t i = 0; i < G_N_ELEMENTS(fields) && length <= 0; ++i) {
		int index = X509_NAME_get_index_by_NID(subject, fields[i], -1);
		OPENSSL_free(name);
		name = NULL;
		length = index < 0 ? -1
		                   : ASN1_STRING_to_UTF8(
		                         &name,
		                         X509_NAME_ENTRY_get_data(
		                             X509_NAME_get_entry(subject, index)
		                         )
		                     );
	}
	char* plain = length > 0 ? g_strndup((const char*)name, (gsize)length)
	                         : NULL;
	OPENSSL_free(name);
	X509_free(x509);
	char* svg = plain ? appearance_text_svg(plain) : NULL;
	g_free(plain);
	return svg;
}
char* appearance_text_svg(const char* name) {
	int           length = (int)strlen(name);
	char*         escaped = g_markup_escape_text(name, -1);
	PangoContext* context = pango_font_map_create_context(
	    pango_cairo_font_map_get_default()
	);
	PangoLayout*          layout = pango_layout_new(context);
	PangoFontDescription* font = pango_font_description_from_string(
	    "sans-serif"
	);
	pango_font_description_set_absolute_size(font, 26 * PANGO_SCALE);
	pango_font_description_set_weight(font, PANGO_WEIGHT_BOLD);
	pango_layout_set_font_description(layout, font);
	if (length > 0) {
		pango_layout_set_text(layout, name, length);
	}
	int name_width = 0;
	pango_layout_get_pixel_size(layout, &name_width, NULL);
	char font_size[G_ASCII_DTOSTR_BUF_SIZE];
	g_ascii_dtostr(
	    font_size,
	    sizeof(font_size),
	    name_width > 550 ? 26.0 * 550 / name_width : 26.0
	);
	pango_font_description_free(font);
	g_object_unref(layout);
	g_object_unref(context);

	if (!escaped) {
		return NULL;
	}
	GDateTime* now = g_date_time_new_from_unix_local(
	    g_get_real_time() / G_USEC_PER_SEC
	);
	char* date = g_date_time_format(now, "%Y-%m-%dT%H:%M:%S%:z");
	char* signed_text = g_markup_escape_text(_("Digitally signed"), -1);
	char* notice = g_markup_escape_text(
	    _("Graphical representation. Verify validity."),
	    -1
	);
	/* Measure with the same font engine used by librsvg to fit long names. */
	char* svg = g_strdup_printf(
	    "<svg xmlns='http://www.w3.org/2000/svg' width='600' height='200' "
	    "viewBox='0 0 600 200'>"
	    "<rect x='2' y='2' width='596' height='196' rx='12' fill='white' "
	    "stroke='#24516c' stroke-width='4'/>"
	    "<g font-family='sans-serif' fill='#183747'>"
	    "<text x='24' y='43' font-size='%s' font-weight='bold'>%s</text>"
	    "<text x='24' y='83' font-size='25'>%s</text>"
	    "<text x='24' y='124' font-size='23'>%s</text>"
	    "<text x='24' y='172' font-size='17'>%s</text></g></svg>",
	    font_size,
	    escaped,
	    signed_text,
	    date,
	    notice
	);
	g_free(signed_text);
	g_free(notice);
	g_free(date);
	g_date_time_unref(now);
	g_free(escaped);
	return svg;
}

static bool dimensions(
    libssig_pdf_appearance* a,
    double                  width,
    double                  height,
    bool                    preview
) {
	if (!isfinite(width) || !isfinite(height) || width <= 0 || height <= 0) {
		return false;
	}
	if (preview) {
		double ratio = width / height;
		double low = fmax(
		    SIGN_APPEARANCE_MIN_SIZE,
		    SIGN_APPEARANCE_MIN_SIZE * ratio
		);
		double high = fmin(
		    SIGN_APPEARANCE_MAX_SIZE,
		    SIGN_APPEARANCE_MAX_SIZE * ratio
		);
		if (low > high) {
			return false;
		}
		a->width = fmax(low, fmin(high, SIGN_APPEARANCE_DEFAULT_WIDTH));
		a->height = 0;
	}
	if (a->height > 0) {
		a->width = a->height * width / height;
	} else {
		if (a->width == 0) {
			a->width = SIGN_APPEARANCE_DEFAULT_WIDTH;
		}
		a->height = a->width * height / width;
	}
	return isfinite(a->width) && isfinite(a->height) &&
	       a->width >= SIGN_APPEARANCE_MIN_SIZE &&
	       a->height >= SIGN_APPEARANCE_MIN_SIZE &&
	       a->width <= SIGN_APPEARANCE_MAX_SIZE &&
	       a->height <= SIGN_APPEARANCE_MAX_SIZE;
}

static bool svg_size(RsvgHandle* svg, double* width, double* height) {
	*width = *height = 0;
	if (!rsvg_handle_get_intrinsic_size_in_pixels(svg, width, height)) {
		gboolean      has_box = FALSE;
		RsvgRectangle box;
		rsvg_handle_get_intrinsic_dimensions(
		    svg,
		    NULL,
		    NULL,
		    NULL,
		    NULL,
		    &has_box,
		    &box
		);
		if (!has_box) {
			return false;
		}
		*width = box.width;
		*height = box.height;
	}
	return isfinite(*width) && isfinite(*height) && *width > 0 && *height > 0;
}

static void surface_pixels(
    cairo_surface_t*        surface,
    libssig_pdf_appearance* a,
    unsigned char**         pixels
) {
	int w = cairo_image_surface_get_width(surface),
	    h = cairo_image_surface_get_height(surface);
	cairo_surface_flush(surface);
	unsigned char* data = cairo_image_surface_get_data(surface);
	int            stride = cairo_image_surface_get_stride(surface);
	*pixels = g_malloc((size_t)w * h * 4);
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			uint32_t pixel;
			memcpy(&pixel, data + (size_t)y * stride + x * 4, 4);
			unsigned int   alpha = pixel >> 24;
			unsigned char* dest = *pixels + ((size_t)y * w + x) * 4;
			dest[0] = alpha ? (((pixel >> 16) & 255) * 255 + alpha / 2) / alpha
			                : 0;
			dest[1] = alpha ? (((pixel >> 8) & 255) * 255 + alpha / 2) / alpha
			                : 0;
			dest[2] = alpha ? ((pixel & 255) * 255 + alpha / 2) / alpha : 0;
			dest[3] = alpha;
		}
	}
	a->pixel_width = w;
	a->pixel_height = h;
	a->row_stride = (size_t)w * 4;
}

static bool svg_pixels(
    RsvgHandle*             svg,
    libssig_pdf_appearance* a,
    unsigned char**         pixels,
    bool                    preview
) {
	double width = 0, height = 0;
	if (!svg_size(svg, &width, &height)) {
		return false;
	}
	if (!dimensions(a, width, height, preview)) {
		return false;
	}
	int              w = (int)ceil(a->width * 4), h = (int)ceil(a->height * 4);
	cairo_surface_t* surface = cairo_image_surface_create(
	    CAIRO_FORMAT_ARGB32,
	    w,
	    h
	);
	cairo_t*      cr = cairo_create(surface);
	RsvgRectangle viewport = {0, 0, w, h};
	GError*       error = NULL;
	bool ok = rsvg_handle_render_document(svg, cr, &viewport, &error) &&
	          cairo_status(cr) == CAIRO_STATUS_SUCCESS &&
	          cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS;
	if (ok) {
		surface_pixels(surface, a, pixels);
	}
	g_clear_error(&error);
	cairo_destroy(cr);
	cairo_surface_destroy(surface);
	return ok;
}

/* Render image and certificate information as one bounded appearance. Both
 * preview and signing call this compositor; no temporary image files are used.
 */
static bool composed_pixels(
    RsvgHandle*                image_svg,
    GdkPixbuf*                 image,
    const libssig_certificate* cert,
    const char*                preview_svg,
    libssig_pdf_appearance*    a,
    unsigned char**            pixels
) {
	double iw = 0, ih = 0;
	if (image_svg) {
		if (!svg_size(image_svg, &iw, &ih)) {
			return false;
		}
	} else {
		iw = gdk_pixbuf_get_width(image);
		ih = gdk_pixbuf_get_height(image);
	}
	double ratio = iw / ih;
	if (!isfinite(ratio) || ratio <= 0) {
		return false;
	}
	double image_height = fmin(300.0, 600.0 / ratio);
	double image_width = image_height * ratio;
	double height = image_height + 216.0;
	if (!dimensions(a, 600, height, preview_svg != NULL)) {
		return false;
	}
	char* text = preview_svg ? g_strdup(preview_svg)
	                         : appearance_default_svg(cert);
	if (!text) {
		return false;
	}
	RsvgHandle* stamp = rsvg_handle_new_from_data(
	    (const guint8*)text,
	    strlen(text),
	    NULL
	);
	g_free(text);
	if (!stamp) {
		return false;
	}
	int              w = (int)ceil(a->width * 4), h = (int)ceil(a->height * 4);
	cairo_surface_t* surface = cairo_image_surface_create(
	    CAIRO_FORMAT_ARGB32,
	    w,
	    h
	);
	cairo_t* cr = cairo_create(surface);
	cairo_scale(cr, w / 600.0, h / height);
	RsvgRectangle viewport =
	    {(600 - image_width) / 2, 0, image_width, image_height};
	bool ok = true;
	cairo_save(cr);
	cairo_rectangle(
	    cr,
	    viewport.x,
	    viewport.y,
	    viewport.width,
	    viewport.height
	);
	cairo_clip(cr);
	if (image_svg) {
		ok = rsvg_handle_render_document(image_svg, cr, &viewport, NULL);
	} else {
		int pw = gdk_pixbuf_get_width(image), ph = gdk_pixbuf_get_height(image);
		cairo_surface_t* raster = cairo_image_surface_create(
		    CAIRO_FORMAT_ARGB32,
		    pw,
		    ph
		);
		cairo_surface_flush(raster);
		unsigned char* dest = cairo_image_surface_get_data(raster);
		int            stride = cairo_image_surface_get_stride(raster);
		int            channels = gdk_pixbuf_get_n_channels(image),
		    source_stride = gdk_pixbuf_get_rowstride(image);
		const unsigned char* source = gdk_pixbuf_read_pixels(image);
		for (int y = 0;
		     y < ph && cairo_surface_status(raster) == CAIRO_STATUS_SUCCESS;
		     ++y) {
			for (int x = 0; x < pw; ++x) {
				const unsigned char* p = source + (size_t)y * source_stride +
				                         x * channels;
				unsigned int alpha = channels == 4 ? p[3] : 255;
				uint32_t     pixel = (alpha << 24) |
				                 (((p[0] * alpha + 127) / 255) << 16) |
				                 (((p[1] * alpha + 127) / 255) << 8) |
				                 ((p[2] * alpha + 127) / 255);
				memcpy(dest + (size_t)y * stride + x * 4, &pixel, 4);
			}
		}
		cairo_surface_mark_dirty(raster);
		cairo_save(cr);
		cairo_translate(cr, viewport.x, 0);
		cairo_scale(cr, image_width / pw, image_height / ph);
		cairo_set_source_surface(cr, raster, 0, 0);
		cairo_rectangle(cr, 0, 0, pw, ph);
		cairo_fill(cr);
		cairo_restore(cr);
		ok = cairo_surface_status(raster) == CAIRO_STATUS_SUCCESS;
		cairo_surface_destroy(raster);
	}
	cairo_restore(cr);
	RsvgRectangle label = {0, image_height + 16, 600, 200};
	ok = ok && rsvg_handle_render_document(stamp, cr, &label, NULL) &&
	     cairo_status(cr) == CAIRO_STATUS_SUCCESS &&
	     cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS;
	if (ok) {
		surface_pixels(surface, a, pixels);
	}
	cairo_destroy(cr);
	cairo_surface_destroy(surface);
	g_object_unref(stamp);
	return ok;
}

static void raster_size(
    GdkPixbufLoader* loader,
    int              width,
    int              height,
    gpointer         data
) {
	bool* oversized = data;
	if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
		*oversized = true;
		gdk_pixbuf_loader_set_size(loader, 1, 1);
	}
}

static bool prepare(
    const sign_options*        args,
    const libssig_certificate* cert,
    libssig_pdf_appearance*    appearance,
    unsigned char**            pixels,
    const char*                default_svg
) {
	*appearance = args->appearance;
	*pixels = NULL;
	if (!appearance->page_number) {
		return true;
	}
	sign_buffer source = {0};
	char        error[256];
	if (args->appearance_file) {
		if (!file_load(args->appearance_file, &source, error, sizeof(error))) {
			fprintf(stderr, "sign: %s\n", error);
			return false;
		}
	} else {
		source.data = (unsigned char*)(default_svg
		                                   ? g_strdup(default_svg)
		                                   : appearance_default_svg(cert));
		if (!source.data) {
			fputs(
			    "sign: cannot read the certificate name for the default mark; "
			    "use -i FILE\n",
			    stderr
			);
			return false;
		}
		source.len = strlen((char*)source.data);
	}
	bool        ok = false;
	RsvgHandle* svg = NULL;
	if (source.len <= 32 * 1024 * 1024) {
		svg = rsvg_handle_new_from_data(source.data, source.len, NULL);
	}
	if (svg) {
		ok = args->appearance_file && args->appearance_information
		         ? composed_pixels(
		               svg,
		               NULL,
		               cert,
		               default_svg,
		               appearance,
		               pixels
		           )
		         : svg_pixels(svg, appearance, pixels, default_svg != NULL);
		g_object_unref(svg);
	} else if (source.len <= 32 * 1024 * 1024) {
		GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
		bool             oversized = false;
		g_signal_connect(
		    loader,
		    "size-prepared",
		    G_CALLBACK(raster_size),
		    &oversized
		);
		bool loaded = gdk_pixbuf_loader_write(
		    loader,
		    source.data,
		    source.len,
		    NULL
		);
		loaded = gdk_pixbuf_loader_close(loader, NULL) && loaded;
		GdkPixbuf* image = loaded && !oversized
		                       ? gdk_pixbuf_loader_get_pixbuf(loader)
		                       : NULL;
		GdkPixbuf* oriented = image
		                          ? gdk_pixbuf_apply_embedded_orientation(image)
		                          : NULL;
		if (oriented && args->appearance_file && args->appearance_information) {
			ok = composed_pixels(
			    NULL,
			    oriented,
			    cert,
			    default_svg,
			    appearance,
			    pixels
			);
			g_object_unref(oriented);
		} else if (oriented) {
			int w = gdk_pixbuf_get_width(oriented),
			    h = gdk_pixbuf_get_height(oriented);
			ok = dimensions(appearance, w, h, default_svg != NULL);
			if (ok) {
				int channels = gdk_pixbuf_get_n_channels(oriented);
				int stride = gdk_pixbuf_get_rowstride(oriented);
				const unsigned char* data = gdk_pixbuf_read_pixels(oriented);
				*pixels = g_malloc((size_t)w * h * 4);
				for (int y = 0; y < h; ++y) {
					for (int x = 0; x < w; ++x) {
						const unsigned char* src = data + (size_t)y * stride +
						                           x * channels;
						unsigned char* dst = *pixels + ((size_t)y * w + x) * 4;
						memcpy(dst, src, 3);
						dst[3] = channels == 4 ? src[3] : 255;
					}
				}
				appearance->pixel_width = w;
				appearance->pixel_height = h;
				appearance->row_stride = (size_t)w * 4;
			}
			g_object_unref(oriented);
		}
		g_object_unref(loader);
	}
	free(source.data);
	if (!ok) {
		fputs(
		    "sign: invalid signature image or derived size outside 12–720 PDF "
		    "points (raster limit: 4096×4096)\n",
		    stderr
		);
	}
	appearance->pixels = *pixels;
	return ok;
}

bool appearance_prepare(
    const sign_options*        args,
    const libssig_certificate* cert,
    libssig_pdf_appearance*    appearance,
    unsigned char**            pixels
) {
	return prepare(args, cert, appearance, pixels, NULL);
}
bool appearance_preview(
    const sign_options*     args,
    const char*             default_svg,
    libssig_pdf_appearance* appearance,
    unsigned char**         pixels
) {
	return prepare(args, NULL, appearance, pixels, default_svg);
}
