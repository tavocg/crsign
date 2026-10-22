/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <gio/gio.h>
#include <sign/pin.h>
#include <sign/terminal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void pin_free(sign_buffer* pin) {
	if (pin->data) {
		volatile unsigned char* cursor = pin->data;
		for (size_t i = 0; i <= pin->len; ++i) {
			cursor[i] = 0;
		}
		free(pin->data);
	}
	*pin = (sign_buffer){0};
}

bool pin_read(const char* const* command, sign_buffer* pin) {
	if (!command) {
		return terminal_pin_read(pin);
	}
	/* stdin defaults to the null device. Do not expose helper stderr or use a
	 * shell implicitly; either could leak secrets into diagnostics/JSON. */
	GSubprocess* child = g_subprocess_newv(
	    command,
	    G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
	    NULL
	);
	if (!child) {
		fputs("sign: cannot start pincmd\n", stderr);
		return false;
	}
	/* Room for a maximum-length PIN, CRLF, and one byte to detect overflow.
	 * Mark the entire allocation for wiping, even after an unsuccessful read.
	 */
	const size_t  capacity = SIGN_PIN_LIMIT + 3;
	sign_buffer   output = {.data = malloc(capacity), .len = capacity - 1};
	bool          ok = output.data != NULL;
	size_t        used = 0;
	GInputStream* stream = g_subprocess_get_stdout_pipe(child);
	while (ok && used < capacity) {
		gssize count = g_input_stream_read(
		    stream,
		    output.data + used,
		    capacity - used,
		    NULL,
		    NULL
		);
		if (count < 0) {
			ok = false;
		} else if (!count) {
			break;
		} else {
			used += (size_t)count;
		}
	}
	if (used == capacity) {
		ok = false;
	}
	if (!ok) {
		g_subprocess_force_exit(child);
	}
	if (!g_subprocess_wait_check(child, NULL, NULL)) {
		ok = false;
	}
	g_object_unref(child);
	if (ok) {
		if (used && output.data[used - 1] == '\n') {
			--used;
			if (used && output.data[used - 1] == '\r') {
				--used;
			}
		}
		ok = used > 0 && used <= SIGN_PIN_LIMIT &&
		     !memchr(output.data, 0, used) &&
		     !memchr(output.data, '\n', used) &&
		     !memchr(output.data, '\r', used) &&
		     g_utf8_validate((const char*)output.data, (gssize)used, NULL);
	}
	if (ok) {
		pin->data = malloc(used + 1);
		ok = pin->data != NULL;
		if (ok) {
			memcpy(pin->data, output.data, used);
			pin->data[used] = 0;
			pin->len = used;
		}
	}
	pin_free(&output);
	if (!ok) {
		fputs("sign: pincmd failed or returned an invalid PIN\n", stderr);
	}
	return ok;
}
