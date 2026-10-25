/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_FILE_STATE_H
#define SIGN_FILE_STATE_H
#include <gio/gio.h>
#include <stdbool.h>
/* Permission and transient I/O errors do not mean that a document disappeared.
 */
static inline bool file_is_missing(GFile* file) {
	if (!g_file_is_native(file)) {
		return false;
	}
	GError*    error = NULL;
	GFileInfo* info = g_file_query_info(
	    file,
	    G_FILE_ATTRIBUTE_STANDARD_TYPE,
	    G_FILE_QUERY_INFO_NONE,
	    NULL,
	    &error
	);
	bool missing = g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
	               g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY);
	if (info) {
		g_object_unref(info);
	}
	g_clear_error(&error);
	return missing;
}
#endif
