/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/modules.h>
#include <stdio.h>

libssig_status modules_open(const sign_options* args, libssig_context* ctx) {
	if (!args->modules.count) {
		return libssig_context_open(ctx);
	}
	libssig_status status = libssig_context_open_modules(ctx, &args->modules);
	if (status != LIBSSIG_OK) {
		fputs(
		    "Cannot load or initialize the configured PKCS#11 modules\n",
		    stderr
		);
	}
	return status;
}
