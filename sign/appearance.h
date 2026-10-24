/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <sign/options.h>

/* Generated SVG and pixels are owned by the caller. */
char* appearance_default_svg(const libssig_certificate* cert);
char* appearance_text_svg(const char* name);

/* Pixels are owned by the caller and released with g_free(). With a custom
 * image, appearance_information appends certificate information underneath;
 * dimensions describe the combined image. */
bool appearance_prepare(
    const sign_options*        args,
    const libssig_certificate* cert,
    libssig_pdf_appearance*    appearance,
    unsigned char**            pixels
);
/* Same decoding and size limits as signing, with an illustrative default SVG
 * for a preview before a certificate is available. Chooses a usable size. */
bool appearance_preview(
    const sign_options*     args,
    const char*             default_svg,
    libssig_pdf_appearance* appearance,
    unsigned char**         pixels
);
