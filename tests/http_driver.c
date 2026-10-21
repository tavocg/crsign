/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/http.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char** argv) {
	if (argc != 3) {
		return 2;
	}
	int                 kind = atoi(argv[1]);
	const unsigned char request[] = "query";
	libssig_signature   response = {0};
	libssig_status      status = http_exchange(
        NULL,
        (libssig_fetch_kind)kind,
        argv[2],
        kind < 2 ? request : NULL,
        kind < 2 ? sizeof(request) - 1 : 0,
        &response
    );
	if (status == LIBSSIG_OK) {
		fwrite(response.data, 1, response.len, stdout);
	}
	libssig_signature_free(&response);
	return status == LIBSSIG_OK ? 0 : 1;
}
