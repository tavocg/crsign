/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/terminal.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
	sign_buffer pin = {0};
	if (!terminal_pin_read(&pin)) {
		return 1;
	}
	const char* expected = argc > 1 && !strcmp(argv[1], "unicode")
	                           ? "12\xc3\xb1"
	                       : argc > 1 && !strcmp(argv[1], "empty") ? ""
	                                                               : "1234";
	bool        ok;
	if (argc > 1 && !strcmp(argv[1], "limit")) {
		ok = pin.len == 1024;
		for (size_t i = 0; i < pin.len; ++i) {
			ok = ok && pin.data[i] == '7';
		}
	} else {
		ok = pin.len == strlen(expected) &&
		     !memcmp(pin.data, expected, pin.len);
	}
	ok = ok && pin.data[pin.len] == 0;
	pin_free(&pin);
	ok = ok && !pin.data && !pin.len;
	pin_free(&pin);
	if (!ok) {
		return 2;
	}
	puts("PIN accepted; buffer cleared");
	return 0;
}
