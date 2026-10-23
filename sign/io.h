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
/* Paths use the native filename encoding (UTF-8 on Windows). Buffers must be
 * initialized to zero; free successful results with free(). The extra trailing
 * NUL is not included in len. A NULL path represents an omitted optional file.
 * file_load returns an error message; file_read reports it on stderr. */
bool file_load(
    const char*  path,
    sign_buffer* bytes,
    char*        error,
    size_t       error_size
);
bool file_read(const char* path, sign_buffer* bytes);

bool file_save(
    const char*          path,
    const unsigned char* data,
    size_t               len,
    char*                error,
    size_t               error_size
);

bool file_write(const char* path, const unsigned char* data, size_t len);

#endif
