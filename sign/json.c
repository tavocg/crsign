/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/json.h>
#include <stdio.h>
#include <string.h>

/* Preserve valid UTF-8; escape malformed bytes to keep output valid JSON. */
static size_t utf8_len(const unsigned char* text, size_t len) {
	unsigned char c = text[0];
	size_t        count = c >= 0xc2 && c <= 0xdf   ? 2
	                      : c >= 0xe0 && c <= 0xef ? 3
	                      : c >= 0xf0 && c <= 0xf4 ? 4
	                                               : 0;
	if (!count || count > len) {
		return 0;
	}
	for (size_t i = 1; i < count; ++i) {
		if (text[i] < 0x80 || text[i] > 0xbf) {
			return 0;
		}
	}

	if ((c == 0xe0 && text[1] < 0xa0) || (c == 0xed && text[1] >= 0xa0) ||
	    (c == 0xf0 && text[1] < 0x90) || (c == 0xf4 && text[1] >= 0x90)) {
		return 0;
	}
	return count;
}

void json_text(const unsigned char* text, size_t len) {
	if (!text) {
		fputs("null", stdout);
		return;
	}
	putchar('"');
	for (size_t i = 0; i < len; ++i) {
		unsigned char c = text[i];
		if (c == '"' || c == '\\') {
			putchar('\\');
			putchar(c);
		} else if (c < 0x20) {
			printf("\\u%04x", (unsigned)c);
		} else if (c >= 0x80) {
			size_t count = utf8_len(text + i, len - i);
			if (count) {
				fwrite(text + i, 1, count, stdout);
				i += count - 1;
			} else {
				printf("\\u%04x", (unsigned)c);
			}
		} else {
			putchar(c);
		}
	}
	putchar('"');
}

void json_string(const char* text) {
	json_text((const unsigned char*)text, text ? strlen(text) : 0);
}

void json_hex(const unsigned char* data, size_t len) {
	if (!data) {
		fputs("null", stdout);
		return;
	}
	putchar('"');
	for (size_t i = 0; i < len; ++i) {
		printf("%02x", (unsigned)data[i]);
	}
	putchar('"');
}

void json_strings(const libssig_string_list* items) {
	putchar('[');
	for (size_t i = 0; i < items->count; ++i) {
		if (i) {
			putchar(',');
		}
		json_string(items->items[i]);
	}
	putchar(']');
}

const char* json_bool(bool value) {
	return value ? "true" : "false";
}
