/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_EXECUTION_H
#define SIGN_EXECUTION_H
#include <gio/gio.h>
#include <sign/io.h>
#include <sign/options.h>

/* Called only while executing, after selecting a token that requires a PIN.
 * Return a malloc-compatible buffer, including for an empty PIN. The engine
 * wipes/frees it on every path. Never log a PIN or include it in an error.
 * NULL provider fails without opening a terminal or reading configuration. */
typedef bool (*sign_pin_provider)(
    const libssig_token* token,
    sign_buffer*         pin,
    void*                user_data,
    GError**             error
);
typedef struct {
	sign_pin_provider request_pin;
	void*             user_data;
} sign_execution;

typedef struct {
	libssig_format format;
	unsigned long  ck_rv;
	size_t         len;
} sign_document_result;

/* Synchronous single-file engine; no CLI output or implicit profile loading.
 * Preserves exclusive output creation and removes partial writes on failure. */
bool sign_document_execute(
    const sign_options*   options,
    const sign_execution* execution,
    sign_document_result* result,
    GError**              error
);
#endif
