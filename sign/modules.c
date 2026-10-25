/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <glib.h>
#include <libssig/provider.h>
#include <sign/modules.h>
#include <stdio.h>

#ifndef SIGN_TEST_EXTERNAL_MODULES
/* OpenSC is process-wide, like the GUI's long-lived inventory worker. Never
 * finalize it when an individual signing context closes: other contexts may
 * still be enumerating or signing. The OS reclaims it at process exit. */
extern CK_RV crsign_opensc_get_function_list(CK_FUNCTION_LIST_PTR_PTR list);
#ifdef _WIN32
extern int module_init(void);
#endif
static CK_FUNCTION_LIST* embedded_provider(void) {
	static gsize             initialized;
	static CK_FUNCTION_LIST* provider;
	if (g_once_init_enter(&initialized)) {
#ifdef _WIN32
		/* Static linking does not invoke OpenSC's DLL_PROCESS_ATTACH hook. */
		module_init();
#endif
		CK_FUNCTION_LIST*    candidate = NULL;
		CK_C_INITIALIZE_ARGS init = {.flags = CKF_OS_LOCKING_OK};
		if (crsign_opensc_get_function_list(&candidate) == CKR_OK &&
		    candidate) {
			CK_RV rv = candidate->C_Initialize(&init);
			if (rv == CKR_OK || rv == CKR_CRYPTOKI_ALREADY_INITIALIZED) {
				provider = candidate;
			}
		}
		g_once_init_leave(&initialized, 1);
	}
	return provider;
}
#endif

libssig_status modules_open(const sign_options* args, libssig_context* ctx) {
	if (!args->modules.count) {
#ifdef SIGN_TEST_EXTERNAL_MODULES
		return libssig_context_open(ctx);
#else
		libssig_status status = libssig_context_open(ctx);
		if (status != LIBSSIG_OK) {
			return status;
		}
		CK_FUNCTION_LIST* provider = embedded_provider();
		if (!provider) {
			g_debug(
			    "Embedded OpenSC could not initialize; retaining p11-kit "
			    "providers"
			);
			return LIBSSIG_OK;
		}
		status = libssig_context_add_provider(ctx, provider);
		if (status != LIBSSIG_OK) {
			libssig_context_close(ctx);
		}
		return status;
#endif
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
