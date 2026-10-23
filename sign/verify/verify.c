/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "internal.h"
#include <sign/execution.h>

bool verify_run(
    const sign_args*   args,
    const sign_buffer* input,
    const sign_trust*  trust
) {
	libssig_verify_result result = {0};
	libssig_status status = sign_verify_buffers(input, NULL, trust, &result);
	verify_output(args, status, &result);
	libssig_verify_free(&result);
	return status == LIBSSIG_OK;
}
