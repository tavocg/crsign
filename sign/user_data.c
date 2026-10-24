/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <sign/user_data.h>
#include <string.h>
#ifdef G_OS_WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif
static GMutex mutex;
char*         user_data_path(const char* name) {
    return g_build_filename(g_get_user_data_dir(), "crsign", name, NULL);
}
static bool invalid(GError** error, const char* message) {
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, message);
	return false;
}
json_t* user_data_load(const char* name, size_t limit, GError** error) {
	char*  path = user_data_path(name);
	GFile* file = g_file_new_for_path(path);
	g_free(path);
	GFileInputStream* stream = g_file_read(file, NULL, error);
	g_object_unref(file);
	if (!stream) {
		return NULL;
	}
	GFileInfo* info = g_file_input_stream_query_info(
	    stream,
	    G_FILE_ATTRIBUTE_STANDARD_SIZE,
	    NULL,
	    error
	);
	if (!info) {
		g_object_unref(stream);
		return NULL;
	}
	bool bounded = g_file_info_get_size(info) <= (goffset)limit;
	g_object_unref(info);
	if (!bounded) {
		g_object_unref(stream);
		invalid(error, "Data file exceeds its maximum size");
		return NULL;
	}
	GByteArray*   bytes = g_byte_array_new();
	GError*       cause = NULL;
	unsigned char buffer[8192];
	gssize        size;
	while ((size = g_input_stream_read(
	            G_INPUT_STREAM(stream),
	            buffer,
	            sizeof(buffer),
	            NULL,
	            &cause
	        )) > 0) {
		if ((gsize)size > limit - bytes->len) {
			invalid(&cause, "Data file exceeds its maximum size");
			break;
		}
		g_byte_array_append(bytes, buffer, (guint)size);
	}
	g_object_unref(stream);
	if (cause) {
		g_byte_array_unref(bytes);
		g_propagate_error(error, cause);
		return NULL;
	}
	json_error_t parse;
	json_t*      root = json_loadb(
        (const char*)bytes->data,
        bytes->len,
        JSON_REJECT_DUPLICATES,
        &parse
    );
	g_byte_array_unref(bytes);
	if (!root) {
		invalid(error, "Invalid JSON data file");
	}
	return root;
}
int user_data_lock(GError** error) {
	g_mutex_lock(&mutex);
	char* directory = g_build_filename(g_get_user_data_dir(), "crsign", NULL);
	if (g_mkdir_with_parents(directory, 0700) < 0) {
		g_set_error_literal(
		    error,
		    G_IO_ERROR,
		    G_IO_ERROR_PERMISSION_DENIED,
		    "Cannot create user data directory"
		);
		g_free(directory);
		g_mutex_unlock(&mutex);
		return -1;
	}
	char* path = g_build_filename(directory, "state.lock", NULL);
	g_free(directory);
	int fd = g_open(path, O_RDWR | O_CREAT, 0600);
	g_free(path);
	if (fd < 0) {
		g_set_error_literal(
		    error,
		    G_IO_ERROR,
		    G_IO_ERROR_FAILED,
		    "Cannot open user data lock"
		);
		g_mutex_unlock(&mutex);
		return -1;
	}
	gint64 deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
	do {
#ifdef G_OS_WIN32
		OVERLAPPED operation = {0};
		if (LockFileEx(
		        (HANDLE)_get_osfhandle(fd),
		        LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
		        0,
		        1,
		        0,
		        &operation
		    )) {
			return fd;
		}
#else
		struct flock operation = {0};
		operation.l_type = F_WRLCK;
		operation.l_whence = SEEK_SET;
		operation.l_len = 1;
		if (fcntl(fd, F_SETLK, &operation) == 0) {
			return fd;
		}
		if (errno != EACCES && errno != EAGAIN && errno != EINTR) {
			break;
		}
#endif
		g_usleep(10000);
	} while (g_get_monotonic_time() < deadline);
	g_close(fd, NULL);
	g_set_error_literal(
	    error,
	    G_IO_ERROR,
	    G_IO_ERROR_BUSY,
	    "User data is busy"
	);
	g_mutex_unlock(&mutex);
	return -1;
}

void user_data_unlock(int fd) {
#ifdef G_OS_WIN32
	OVERLAPPED operation = {0};
	UnlockFileEx((HANDLE)_get_osfhandle(fd), 0, 1, 0, &operation);
#endif
	g_close(fd, NULL);
	g_mutex_unlock(&mutex);
}
typedef struct {
	GString* text;
	size_t   limit;
} bounded_json;
static int append_json(const char* data, size_t size, void* user_data) {
	bounded_json* output = user_data;
	if (size > output->limit - output->text->len) {
		return -1;
	}
	g_string_append_len(output->text, data, (gssize)size);
	return 0;
}
bool user_data_save(
    const char* name,
    json_t*     root,
    size_t      limit,
    GError**    error
) {
	bounded_json output = {g_string_new(NULL), limit};
	if (json_dump_callback(root, append_json, &output, JSON_COMPACT) < 0) {
		g_string_free(output.text, TRUE);
		return invalid(
		    error,
		    "Data exceeds its maximum size or cannot be encoded"
		);
	}
	char*  path = user_data_path(name);
	GFile* file = g_file_new_for_path(path);
	g_free(path);
	bool ok = g_file_replace_contents(
	    file,
	    output.text->str,
	    output.text->len,
	    NULL,
	    FALSE,
	    G_FILE_CREATE_PRIVATE | G_FILE_CREATE_REPLACE_DESTINATION,
	    NULL,
	    NULL,
	    error
	);
	g_object_unref(file);
	g_string_free(output.text, TRUE);
	return ok;
}
