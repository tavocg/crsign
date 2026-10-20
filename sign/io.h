/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_IO_H
#define SIGN_IO_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
	unsigned char* data;
	size_t         len;
} sign_buffer;

char* file_signed_path(const char* input);
bool  file_read(const char* path, sign_buffer* bytes);

bool file_write(const char* path, const unsigned char* data, size_t len);

#endif
