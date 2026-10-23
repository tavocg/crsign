/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <sign/arg.h>

/* Generated SVG and pixels are owned by the caller. */
char* appearance_default_svg(const libssig_certificate* cert);

/* Pixels are owned by the caller and released with g_free(). */
bool appearance_prepare(
    const sign_args*           args,
    const libssig_certificate* cert,
    libssig_pdf_appearance*    appearance,
    unsigned char**            pixels
);
