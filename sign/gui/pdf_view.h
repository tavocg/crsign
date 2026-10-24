/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_GUI_PDF_VIEW_H
#define SIGN_GUI_PDF_VIEW_H
#include <cairo.h>
#include <gio/gio.h>
#include <sign/options.h>
typedef struct sign_pdf_document sign_pdf_document;
/* NULL without error means a non-PDF file, which can still be queued. */
sign_pdf_document* sign_pdf_open(
    GFile*        file,
    GCancellable* cancellable,
    GError**      error
);
sign_pdf_document* sign_pdf_ref(sign_pdf_document* document);
void               sign_pdf_unref(sign_pdf_document* document);
unsigned int       sign_pdf_pages(const sign_pdf_document* document);
bool               sign_pdf_size(
                  const sign_pdf_document* document,
                  unsigned int             page,
                  double*                  width,
                  double*                  height
              );
cairo_surface_t* sign_pdf_render(
    sign_pdf_document* document,
    unsigned int       page,
    GCancellable*      cancellable,
    GError**           error
);
cairo_surface_t* sign_pdf_thumbnail(
    sign_pdf_document*,
    GCancellable*,
    GError**
);
/* Inputs/outputs are visible-page PDF points after rotation and UserUnit.
 * A zero selection means click/default size. Both dimensions retain aspect. */
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
);
cairo_surface_t* sign_pdf_image(const libssig_pdf_appearance* image);
#endif
