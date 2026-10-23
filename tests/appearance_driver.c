/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Real PDF/CMS signing behind the production image loader, without hardware. */
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <openssl/x509.h>
#include <sign/appearance.h>
#include <sign/arg.h>
#include <sign/io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

libssig_status document_fixture_sign_pdf(
    EVP_PKEY*,
    X509*,
    libssig_sign_params,
    libssig_signature*,
    const libssig_pdf_appearance*
);
int main(int argc, char** argv) {
	sign_args args = arg_parse(argc, argv);
	EVP_PKEY* key = EVP_PKEY_Q_keygen(NULL, NULL, "RSA", 2048);
	X509*     cert = X509_new();
	bool      ok = key && cert && X509_set_version(cert, 2) &&
	          ASN1_INTEGER_set(X509_get_serialNumber(cert), 1) &&
	          X509_set_pubkey(cert, key) &&
	          X509_gmtime_adj(X509_getm_notBefore(cert), -60) &&
	          X509_gmtime_adj(X509_getm_notAfter(cert), 3600) &&
	          X509_NAME_add_entry_by_txt(
	              X509_get_subject_name(cert),
	              "CN",
	              MBSTRING_UTF8,
	              (const unsigned char*)"Ana & Compañía <CR>",
	              -1,
	              -1,
	              0
	          ) &&
	          X509_set_issuer_name(cert, X509_get_subject_name(cert)) &&
	          X509_sign(cert, key, EVP_sha256());
	unsigned char*      der = NULL;
	int                 len = ok ? i2d_X509(cert, &der) : 0;
	libssig_certificate certificate = {
	    .der = der,
	    .der_len = len > 0 ? (size_t)len : 0
	};
	libssig_pdf_appearance appearance = {0};
	unsigned char*         pixels = NULL;
	ok = ok && len > 0 &&
	     appearance_prepare(&args.options, &certificate, &appearance, &pixels);
	const char* preview = g_getenv("SIGN_APPEARANCE_TEST_PREVIEW");
	if (ok && preview && g_str_has_suffix(preview, ".svg")) {
		char* svg = appearance_default_svg(&certificate);
		ok = svg && file_write(preview, (unsigned char*)svg, strlen(svg));
		g_free(svg);
	} else if (ok && pixels && preview) {
		GdkPixbuf* image = gdk_pixbuf_new_from_data(
		    pixels,
		    GDK_COLORSPACE_RGB,
		    TRUE,
		    8,
		    (int)appearance.pixel_width,
		    (int)appearance.pixel_height,
		    (int)appearance.row_stride,
		    NULL,
		    NULL
		);
		ok = image && gdk_pixbuf_save(
		                  image,
		                  preview,
		                  g_str_has_suffix(preview, ".jpg") ? "jpeg" : "png",
		                  NULL,
		                  NULL
		              );
		if (image) {
			g_object_unref(image);
		}
	}
	sign_buffer input = {0};
	ok = ok && file_read(args.options.input, &input);
	libssig_sign_params params = {
	    .data = input.data,
	    .data_len = input.len,
	    .format = LIBSSIG_FORMAT_PADES,
	    .level = LIBSSIG_LEVEL_B_B
	};
	libssig_signature output = {0};
	libssig_status    status = ok ? document_fixture_sign_pdf(
                                     key,
                                     cert,
                                     params,
                                     &output,
                                     appearance.page_number ? &appearance : NULL
                                 )
	                              : LIBSSIG_ERROR_ARGUMENT;
	ok = ok && status == LIBSSIG_OK;
	if (ok) {
		libssig_verify_params verify = {
		    .data = output.data,
		    .data_len = output.len
		};
		libssig_verify_result result = {0};
		ok = libssig_verify(&verify, &result) == LIBSSIG_OK &&
		     result.signature_valid;
		libssig_verify_free(&result);
	}
	ok = ok && args.options.output && file_write(args.options.output, output.data, output.len);
	if (!ok) {
		fprintf(stderr, "appearance fixture failed: %d\n", status);
	}
	libssig_signature_free(&output);
	free(input.data);
	g_free(pixels);
	OPENSSL_free(der);
	X509_free(cert);
	EVP_PKEY_free(key);
	arg_free(&args);
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
