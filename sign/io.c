/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <gio/gio.h>
#include <limits.h>
#include <sign/io.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool file_load(
    const char*  path,
    sign_buffer* bytes,
    char*        error,
    size_t       error_size
) {
	if (!path) {
		return true;
	}

	GError*           cause = NULL;
	GFile*            file = g_file_new_for_path(path);
	GFileInputStream* stream = g_file_read(file, NULL, &cause);
	g_object_unref(file);
	GFileInfo*     info = stream ? g_file_input_stream_query_info(
                                   stream,
                                   G_FILE_ATTRIBUTE_STANDARD_SIZE,
                                   NULL,
                                   &cause
                               )
	                             : NULL;
	goffset        size = info ? g_file_info_get_size(info) : -1;
	bool           ok = info && size >= 0 && size <= INT_MAX;
	unsigned char* data = NULL;
	if (ok) {
		data = malloc((size_t)size + 1);
		if (!data) {
			g_set_error_literal(
			    &cause,
			    G_IO_ERROR,
			    G_IO_ERROR_NO_SPACE,
			    "Cannot allocate input buffer"
			);
			ok = false;
		}
	} else if (info) {
		g_set_error(
		    &cause,
		    G_IO_ERROR,
		    G_IO_ERROR_FAILED,
		    "Cannot read file (maximum %d bytes)",
		    INT_MAX
		);
	}
	if (ok) {
		gsize count = 0;
		ok = g_input_stream_read_all(
		    G_INPUT_STREAM(stream),
		    data,
		    (gsize)size,
		    &count,
		    NULL,
		    &cause
		);
		if (ok && count != (gsize)size) {
			g_set_error_literal(
			    &cause,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "File ended before its reported size"
			);
			ok = false;
		}
	}
	if (stream && !g_input_stream_close(
	                  G_INPUT_STREAM(stream),
	                  NULL,
	                  ok ? &cause : NULL
	              )) {
		ok = false;
	}
	if (info) {
		g_object_unref(info);
	}
	if (stream) {
		g_object_unref(stream);
	}
	if (ok) {
		data[size] = 0;
		*bytes = (sign_buffer){data, (size_t)size};
	} else {
		snprintf(
		    error,
		    error_size,
		    "%s: %s",
		    path,
		    cause ? cause->message : "Cannot read file"
		);
		free(data);
		*bytes = (sign_buffer){0};
	}
	g_clear_error(&cause);
	return ok;
}

bool file_read(const char* path, sign_buffer* bytes) {
	char error[512];
	if (!file_load(path, bytes, error, sizeof(error))) {
		fprintf(stderr, "sign: %s\n", error);
		return false;
	}
	return true;
}

bool file_save(
    const char*          path,
    const unsigned char* data,
    size_t               len,
    char*                detail,
    size_t               detail_size
) {
	GError* error = NULL;
	GFile*  file = g_file_new_for_path(path);
	/* Exclusive creation: never replace an existing file or follow its link. */
	GFileOutputStream*
	     stream = g_file_create(file, G_FILE_CREATE_PRIVATE, NULL, &error);
	bool ok = stream != NULL;
	if (stream) {
		ok = g_output_stream_write_all(
		    G_OUTPUT_STREAM(stream),
		    len ? data : (const unsigned char*)"",
		    len,
		    NULL,
		    NULL,
		    &error
		);
		if (!g_output_stream_close(
		        G_OUTPUT_STREAM(stream),
		        NULL,
		        ok ? &error : NULL
		    )) {
			ok = false;
		}
		g_object_unref(stream);
		if (!ok) {
			g_file_delete(file, NULL, NULL);
		}
	}
	if (!ok) {
		snprintf(
		    detail,
		    detail_size,
		    "%s: %s",
		    path,
		    error ? error->message : "Cannot write file"
		);
	}
	g_clear_error(&error);
	g_object_unref(file);
	return ok;
}

bool file_write(const char* path, const unsigned char* data, size_t len) {
	char error[512];
	if (!file_save(path, data, len, error, sizeof(error))) {
		fprintf(stderr, "sign: %s\n", error);
		return false;
	}
	return true;
}

char* file_signed_path(const char* input) {
	size_t len = strlen(input);
	if (len > SIZE_MAX - sizeof("-signed")) {
		return NULL;
	}
	/* GLib recognizes native separators and Windows drive prefixes. Preserve
	 * the original directory spelling, including mixed separators and UNC. */
	char*       base = g_path_get_basename(input);
	const char* ext = strrchr(base, '.');
	size_t      ext_len = ext && ext != base && len &&
                             !G_IS_DIR_SEPARATOR(input[len - 1])
	                          ? strlen(ext)
	                          : 0;
	size_t      stem_len = len - ext_len;
	g_free(base);
	char* output = malloc(len + sizeof("-signed"));
	if (output) {
		memcpy(output, input, stem_len);
		memcpy(output + stem_len, "-signed", sizeof("-signed") - 1);
		memcpy(
		    output + stem_len + sizeof("-signed") - 1,
		    input + stem_len,
		    ext_len + 1
		);
	}
	return output;
}
