/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <limits.h>
#include <sign/io.h>
#include <stdlib.h>
#include <string.h>
#ifdef G_OS_UNIX
#include <signal.h>
#include <sys/resource.h>
#endif

static char* directory;

static void paths(void) {
	const char* cases[][2] = {
	    {"document.txt", "document-signed.txt"},
	    {"archive.tar.gz", "archive.tar-signed.gz"},
	    {"README", "README-signed"},
	    {".hidden", ".hidden-signed"},
	    {".hidden.txt", ".hidden-signed.txt"},
	    {"folder.with.dots/README", "folder.with.dots/README-signed"},
	    {"subdir/\xc3\xb1 doc.pdf", "subdir/\xc3\xb1 doc-signed.pdf"},
	    {"folder.with.dots/", "folder.with.dots/-signed"},
#ifdef G_OS_WIN32
	    {"C:\\docs.v1\\README", "C:\\docs.v1\\README-signed"},
	    {"C:\\docs.v1\\report.pdf", "C:\\docs.v1\\report-signed.pdf"},
	    {"C:/docs.v1\\report.pdf", "C:/docs.v1\\report-signed.pdf"},
	    {"C:report.pdf", "C:report-signed.pdf"},
	    {"C:.hidden", "C:.hidden-signed"},
	    {"C:\\.hidden", "C:\\.hidden-signed"},
	    {"\\\\server.name\\share.v1\\README",
	     "\\\\server.name\\share.v1\\README-signed"},
	    {"\\\\?\\C:\\docs.v1\\README", "\\\\?\\C:\\docs.v1\\README-signed"},
#else
	    /* Backslashes and colons are ordinary filename characters on Unix. */
	    {"docs.v1\\README", "docs-signed.v1\\README"},
	    {"C:.hidden", "C:-signed.hidden"},
#endif
	};
	for (size_t i = 0; i < G_N_ELEMENTS(cases); ++i) {
		char* actual = file_signed_path(cases[i][0]);
		g_assert_cmpstr(actual, ==, cases[i][1]);
		free(actual);
	}
}

static void normalized_suffixes(void) {
	const char* cases[][2] = {
	    {"firmádo", "document-firmado.pdf"},
	    {"firmado digitalmente", "document-firmado-digitalmente.pdf"},
	    {"fi/rm:ado?", "document-fi-rm-ado.pdf"},
	    {"  --  ", "document-signed.pdf"},
	};
	for (size_t i = 0; i < G_N_ELEMENTS(cases); ++i) {
		char* actual = file_signed_path_with_suffix(
		    "document.pdf",
		    cases[i][0]
		);
		g_assert_cmpstr(actual, ==, cases[i][1]);
		free(actual);
	}
	char* actual = file_signed_path_with_suffix("firmádo.pdf", "firmado");
	g_assert_cmpstr(actual, ==, "firmádo-firmado.pdf");
	free(actual);
}

static void roundtrip(void) {
	char* path = g_build_filename(
	    directory,
	    "\xc3\xb1-\xe6\x96\x87\xe4\xbb\xb6.bin",
	    NULL
	);
	unsigned char data[4096];
	for (size_t i = 0; i < sizeof(data); ++i) {
		data[i] = (unsigned char)i;
	}
	g_assert_true(file_write(path, data, sizeof(data)));
	/* Reject an overwrite, preserving every byte including CR/LF and ^Z. */
	g_assert_false(file_write(path, (const unsigned char*)"other", 5));
	sign_buffer bytes = {0};
	char        error[512];
	g_assert_true(file_load(path, &bytes, error, sizeof(error)));
	g_assert_cmpuint(bytes.len, ==, sizeof(data));
	g_assert_cmpmem(bytes.data, bytes.len, data, sizeof(data));
	g_assert_cmpuint(bytes.data[bytes.len], ==, 0);
	free(bytes.data);
#ifdef G_OS_UNIX
	GFile*     file = g_file_new_for_path(path);
	GFileInfo* info = g_file_query_info(
	    file,
	    G_FILE_ATTRIBUTE_UNIX_MODE,
	    G_FILE_QUERY_INFO_NONE,
	    NULL,
	    NULL
	);
	g_assert_nonnull(info);
	g_assert_cmpuint(
	    g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_MODE) &
	        077,
	    ==,
	    0
	);
	g_object_unref(info);
	g_object_unref(file);
#endif
	g_assert_cmpint(g_remove(path), ==, 0);
	g_free(path);
}

static void empty(void) {
	char* path = g_build_filename(directory, "empty", NULL);
	g_assert_true(file_write(path, NULL, 0));
	sign_buffer bytes = {0};
	g_assert_true(file_read(path, &bytes));
	g_assert_nonnull(bytes.data);
	g_assert_cmpuint(bytes.len, ==, 0);
	g_assert_cmpuint(bytes.data[0], ==, 0);
	free(bytes.data);
	g_assert_cmpint(g_remove(path), ==, 0);
	g_free(path);
}

static void read_errors(void) {
	sign_buffer bytes = {0};
	char        error[512];
	g_assert_true(file_load(NULL, &bytes, error, sizeof(error)));
	g_assert_null(bytes.data);
	char* path = g_build_filename(directory, "missing", NULL);
	g_assert_false(file_load(path, &bytes, error, sizeof(error)));
	g_assert_nonnull(strstr(error, path));
	g_assert_null(bytes.data);
	g_assert_cmpuint(bytes.len, ==, 0);
	g_assert_false(file_load(directory, &bytes, error, sizeof(error)));
	g_assert_null(bytes.data);
	g_free(path);
	path = g_build_filename(directory, "missing-parent", "out", NULL);
	g_assert_false(file_write(path, (const unsigned char*)"x", 1));
	g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
	g_free(path);
}

static void symlink_output(void) {
	char*    target = g_build_filename(directory, "target", NULL);
	char*    path = g_build_filename(directory, "link", NULL);
	GFile*   link = g_file_new_for_path(path);
	GError*  error = NULL;
	gboolean created = g_file_make_symbolic_link(link, target, NULL, &error);
#ifdef G_OS_WIN32
	if (!created &&
	    (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED) ||
	     g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED))) {
		g_test_skip(
		    "Symlink creation requires privileges or filesystem support"
		);
		g_clear_error(&error);
		goto done;
	}
#endif
	g_assert_no_error(error);
	g_assert_true(created);
	/* A dangling destination symlink must not create its target. */
	g_assert_false(file_write(path, (const unsigned char*)"x", 1));
	g_assert_false(g_file_test(target, G_FILE_TEST_EXISTS));
	g_assert_true(file_write(target, (const unsigned char*)"original", 8));
	g_assert_false(file_write(path, (const unsigned char*)"x", 1));
	sign_buffer bytes = {0};
	g_assert_true(file_read(target, &bytes));
	g_assert_cmpmem(bytes.data, bytes.len, "original", 8);
	free(bytes.data);
	g_assert_true(g_file_delete(link, NULL, &error));
	g_assert_no_error(error);
	g_assert_cmpint(g_remove(target), ==, 0);
#ifdef G_OS_WIN32
done:
#endif
	g_object_unref(link);
	g_free(path);
	g_free(target);
}

#ifdef G_OS_UNIX
static void oversized(void) {
	char*          path = g_build_filename(directory, "sparse", NULL);
	GFile*         file = g_file_new_for_path(path);
	GError*        error = NULL;
	GFileIOStream* stream = g_file_create_readwrite(
	    file,
	    G_FILE_CREATE_PRIVATE,
	    NULL,
	    &error
	);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	g_assert_true(g_seekable_truncate(
	    G_SEEKABLE(stream),
	    (goffset)INT_MAX + 1,
	    NULL,
	    &error
	));
	g_assert_no_error(error);
	g_assert_true(g_io_stream_close(G_IO_STREAM(stream), NULL, &error));
	g_assert_no_error(error);
	g_object_unref(stream);
	sign_buffer bytes = {0};
	char        detail[512];
	g_assert_false(file_load(path, &bytes, detail, sizeof(detail)));
	g_assert_nonnull(strstr(detail, "maximum"));
	g_assert_null(bytes.data);
	g_assert_true(g_file_delete(file, NULL, &error));
	g_assert_no_error(error);
	g_object_unref(file);
	g_free(path);
}

static void partial_write(void) {
	char*         path = g_build_filename(directory, "partial", NULL);
	struct rlimit original;
	g_assert_cmpint(getrlimit(RLIMIT_FSIZE, &original), ==, 0);
	struct rlimit limited = original;
	limited.rlim_cur = 128;
	struct sigaction old_action;
	struct sigaction action = {0};
	action.sa_handler = SIG_IGN;
	sigemptyset(&action.sa_mask);
	g_assert_cmpint(sigaction(SIGXFSZ, &action, &old_action), ==, 0);
	g_assert_cmpint(setrlimit(RLIMIT_FSIZE, &limited), ==, 0);
	unsigned char data[1024] = {0};
	bool          ok = file_write(path, data, sizeof(data));
	g_assert_cmpint(setrlimit(RLIMIT_FSIZE, &original), ==, 0);
	g_assert_cmpint(sigaction(SIGXFSZ, &old_action, NULL), ==, 0);
	g_assert_false(ok);
	g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
	g_free(path);
}
#endif

int main(int argc, char** argv) {
	g_test_init(&argc, &argv, NULL);
	GError* error = NULL;
	directory = g_dir_make_tmp("crsign-io-\xc3\xb1-XXXXXX", &error);
	g_assert_no_error(error);
	g_assert_nonnull(directory);
	g_test_add_func("/io/paths", paths);
	g_test_add_func("/io/suffixes", normalized_suffixes);
	g_test_add_func("/io/roundtrip", roundtrip);
	g_test_add_func("/io/empty", empty);
	g_test_add_func("/io/errors", read_errors);
	g_test_add_func("/io/symlinks", symlink_output);
#ifdef G_OS_UNIX
	g_test_add_func("/io/oversized", oversized);
	g_test_add_func("/io/partial-write", partial_write);
#endif
	int result = g_test_run();
	g_assert_cmpint(g_rmdir(directory), ==, 0);
	g_free(directory);
	return result;
}
