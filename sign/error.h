/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_ERROR_H
#define SIGN_ERROR_H
#include <glib.h>
/* Stable error categories; diagnostic text is independent of UI translations.
 */
#define SIGN_ERROR (g_quark_from_static_string("sign-execution-error"))
typedef enum {
	SIGN_ERROR_NO_TOKEN,
	SIGN_ERROR_MANY_TOKENS,
	SIGN_ERROR_NO_CERTIFICATE,
	SIGN_ERROR_MANY_CERTIFICATES,
	SIGN_ERROR_MODULES,
	SIGN_ERROR_IMAGE,
	SIGN_ERROR_PIN,
	SIGN_ERROR_PIN_INCORRECT,
	SIGN_ERROR_PIN_LOCKED,
	SIGN_ERROR_DEVICE_REMOVED,
	SIGN_ERROR_TRUST,
	SIGN_ERROR_TIMESTAMP,
	SIGN_ERROR_UNSUPPORTED,
	SIGN_ERROR_CRYPTO,
	SIGN_ERROR_INPUT,
	SIGN_ERROR_OUTPUT
} sign_error_code;
#endif
