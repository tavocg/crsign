/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <openssl/pem.h>
#include <sign/timestamp/timestamp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char** argv) {
	if (argc != 6) {
		return 2;
	}
	sign_options args = {0};
	args.level = !strcmp(argv[3], "offline") ? LIBSSIG_LEVEL_B_B
	                                         : LIBSSIG_LEVEL_B_LTA;
	args.no_bundled_certs = !strcmp(argv[2], "none");
	if (strcmp(argv[2], "all") && !args.no_bundled_certs) {
		args.cert_bundles[0] = argv[2];
		args.cert_bundle_count = 1;
	}
	if (strcmp(argv[3], "auto") && strcmp(argv[3], "offline")) {
		args.authority = argv[3];
	}
	if (strcmp(argv[4], "-")) {
		args.tsa_intermediates = argv[4];
	}
	if (strcmp(argv[5], "-")) {
		args.ca_file = argv[5];
	}
	sign_trust trust = {0};
	if (!trust_load(&args, &trust)) {
		return 2;
	}
	FILE* stream = fopen(argv[1], "r");
	X509* cert = stream ? PEM_read_X509(stream, NULL, NULL, NULL) : NULL;
	if (stream) {
		fclose(stream);
	}
	unsigned char*      der = NULL;
	int                 len = cert ? i2d_X509(cert, &der) : 0;
	libssig_certificate signer = {
	    .der = der,
	    .der_len = len > 0 ? (size_t)len : 0
	};
	char* url = NULL;
	bool  ok = timestamp_authority(&args, &trust, &signer, &url);
	if (ok) {
		puts(url ? url : "offline");
	}
	free(url);
	OPENSSL_free(der);
	X509_free(cert);
	trust_free(&trust);
	return ok ? 0 : 1;
}
