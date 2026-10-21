/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "internal.h"

bool verify_run(
    const sign_args*   args,
    const sign_buffer* input,
    const sign_trust*  trust
) {
	libssig_verify_params params = {
	    .data = input->data,
	    .data_len = input->len
	};
	trust_verify_params(trust, &params);
	libssig_verify_result result = {0};
	libssig_status        status = libssig_verify(&params, &result);
	verify_output(args, status, &result);
	libssig_verify_free(&result);
	return status == LIBSSIG_OK;
}
