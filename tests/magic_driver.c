/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <libssig/sign/mime.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
	const unsigned char
	      pdf[] = "%PDF-1.7\n1 0 obj\n<< /Type /Catalog >>\nendobj\n%%EOF\n";
	char* type = NULL;
	libssig_status status = libssig_mime_type_detect(
	    pdf,
	    sizeof(pdf) - 1,
	    &type
	);
	if (status == LIBSSIG_OK && type) {
		puts(type);
	}
	free(type);
	return status != LIBSSIG_OK;
}
