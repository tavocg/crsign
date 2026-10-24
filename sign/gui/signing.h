/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_GUI_SIGNING_H
#define SIGN_GUI_SIGNING_H
#include <adwaita.h>
#include <sign/flow.h>
#define SIGN_TYPE_SIGNING (sign_signing_get_type())
G_DECLARE_FINAL_TYPE(SignSigning, sign_signing, SIGN, SIGNING, GtkBox)
GtkWidget*  sign_signing_new(void);
GtkWidget*  sign_signing_profile_selector(SignSigning* self);
void        sign_signing_reload_profiles(SignSigning* self);
void        sign_signing_add_files(SignSigning* self, GListModel* files);
size_t      sign_signing_count(SignSigning* self);
const char* sign_signing_output(SignSigning* self, size_t index);
void        sign_signing_set_output(
           SignSigning* self,
           size_t       index,
           const char*  output
       );
bool sign_signing_is_loading(SignSigning* self);
bool sign_signing_is_busy(SignSigning* self);
bool sign_signing_is_reader(SignSigning* self);
void sign_signing_open_document(SignSigning* self, size_t index);
void sign_signing_show_queue(SignSigning* self);
void sign_signing_set_page(SignSigning* self, unsigned int page);
void sign_signing_set_image(SignSigning* self, const char* file);
bool sign_signing_place(
    SignSigning* self,
    double       x,
    double       y,
    double       width,
    double       height,
    GError**     error
);
void                          sign_signing_remove_appearance(SignSigning* self);
const libssig_pdf_appearance* sign_signing_appearance(
    SignSigning* self,
    size_t       index
);
sign_flow* sign_signing_build_flow(SignSigning* self, GError** error);
void       sign_signing_run(SignSigning* self);
void       sign_signing_clear(SignSigning* self);
/* signing-completed emits a borrowed verification flow after full success.
 * Copy it during the signal to retain it; documents-signed also covers partial
 * success. */
#endif
