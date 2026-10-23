/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_GUI_PDF_READER_H
#define SIGN_GUI_PDF_READER_H
#include <gtk/gtk.h>
#include <sign/gui/pdf_view.h>
#define SIGN_TYPE_PDF_READER (sign_pdf_reader_get_type())
G_DECLARE_FINAL_TYPE(SignPdfReader, sign_pdf_reader, SIGN, PDF_READER, GtkBox)
GtkWidget* sign_pdf_reader_new(void);
void       sign_pdf_reader_set_document(
          SignPdfReader*,
          sign_pdf_document*,
          unsigned int,
          cairo_surface_t*
      );
void sign_pdf_reader_set_stamp(
    SignPdfReader*,
    cairo_surface_t*,
    const libssig_pdf_appearance*
);
void sign_pdf_reader_goto(SignPdfReader*, unsigned int);
bool sign_pdf_reader_page_bounds(
    SignPdfReader*,
    unsigned int,
    graphene_rect_t*
);
GtkWidget* sign_pdf_reader_canvas(SignPdfReader*);
#endif
