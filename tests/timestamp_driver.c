/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/timestamp/timestamp.h>
#include <stdlib.h>
int main(int argc, char** argv) {
	if (argc < 4 || argc > 6) {
		return EXIT_FAILURE;
	}
	sign_args args = {
	    .authority = argv[3],
	    .tsa_ca_file = argc > 4 ? argv[4] : NULL,
	    .tsa_intermediates = argc > 5 ? argv[5] : NULL,
	};
	sign_trust  trust = {0};
	sign_buffer input = {0};
	bool        ok = trust_load(&args, &trust) && file_read(argv[1], &input);
	libssig_signature signature = {.data = input.data, .len = input.len};
	if (ok) {
		ok = timestamp_apply(&args, &trust, &signature);
	}
	if (ok) {
		ok = file_write(argv[2], signature.data, signature.len);
	}
	libssig_signature_free(&signature);
	trust_free(&trust);
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
