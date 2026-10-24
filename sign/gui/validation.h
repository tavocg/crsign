/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_GUI_VALIDATION_H
#define SIGN_GUI_VALIDATION_H
#include <adwaita.h>
#include <sign/flow.h>
#define SIGN_TYPE_VALIDATION (sign_validation_get_type())
G_DECLARE_FINAL_TYPE(SignValidation, sign_validation, SIGN, VALIDATION, GtkBox)
GtkWidget* sign_validation_new(void);
/* File-dialog and drag/drop entry point. GListModel contains GFile objects.
 * One single-document flow runs at a time; individual failures do not stop
 * the remaining independent files. These methods belong to the GTK thread. */
void   sign_validation_add_files(SignValidation* self, GListModel* files);
bool   sign_validation_is_busy(SignValidation* self);
size_t sign_validation_count(SignValidation* self);
/* NULL until this item finishes. Borrowed until clear or retry. */
const sign_flow_document_result* sign_validation_result(
    SignValidation* self,
    size_t          index
);
void sign_validation_set_content(
    SignValidation* self,
    size_t          index,
    GFile*          content
);
/* Copies a verification flow before starting work, preserving trust and
 * detached content. */
bool sign_validation_add_flow(SignValidation*, const sign_flow*, GError**);
#endif
