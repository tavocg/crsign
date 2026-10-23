/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <sign/runtime.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef G_OS_WIN32
#include <glib/gwin32.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

static char* installation_prefix(void) {
#ifdef G_OS_WIN32
	return g_win32_get_package_installation_directory_of_module(NULL);
#else
	char* executable = NULL;
#ifdef __APPLE__
	uint32_t size = 0;
	_NSGetExecutablePath(NULL, &size);
	char* buffer = g_malloc(size);
	if (_NSGetExecutablePath(buffer, &size) == 0) {
		executable = realpath(buffer, NULL);
	}
	g_free(buffer);
#else
	executable = g_file_read_link("/proc/self/exe", NULL);
#endif
	if (!executable) {
		return NULL;
	}
	char* bin = g_path_get_dirname(executable);
	char* prefix = g_path_get_dirname(bin);
	g_free(bin);
	free(executable);
	return prefix;
#endif
}
char* runtime_install_path(const char* path) {
	if (g_path_is_absolute(path)) {
		return g_strdup(path);
	}
	char* prefix = installation_prefix();
	char* result = prefix ? g_build_filename(prefix, path, NULL) : NULL;
	g_free(prefix);
	return result;
}
#ifdef SIGN_WITH_GUI
static void resource_env(const char* name, const char* path) {
	if (!g_getenv(name) && g_file_test(path, G_FILE_TEST_EXISTS)) {
		g_setenv(name, path, true);
	}
}
static bool pixbuf_cache(const char* prefix) {
	if (g_getenv("GDK_PIXBUF_MODULE_FILE")) {
		return true;
	}
	char* template_path = g_build_filename(
	    prefix,
	    "share",
	    "crsign",
	    "loaders.cache.in",
	    NULL
	);
	if (!g_file_test(template_path, G_FILE_TEST_EXISTS)) {
		g_free(template_path);
		return true;
	}
	char* contents = NULL;
	bool  ok = g_file_get_contents(template_path, &contents, NULL, NULL);
	g_free(template_path);
	if (!ok) {
		return false;
	}
	char* normalized = g_strdup(prefix);
	for (char* p = normalized; *p; ++p) {
		if (*p == '\\') {
			*p = '/';
		}
	}
	char*  escaped = g_strescape(normalized, "");
	char** parts = g_strsplit(contents, "@CRSIGN_PREFIX@", -1);
	char*  output = g_strjoinv(escaped, parts);
	char*  cache = g_build_filename(g_get_user_cache_dir(), "crsign", NULL);
	char*  hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, prefix, -1);
	char*  filename = g_strdup_printf("pixbuf-%s.cache", hash);
	char*  path = g_build_filename(cache, filename, NULL);
	ok = g_mkdir_with_parents(cache, 0700) == 0 &&
	     g_file_set_contents(path, output, -1, NULL) &&
	     g_setenv("GDK_PIXBUF_MODULE_FILE", path, true);
	g_free(path);
	g_free(filename);
	g_free(hash);
	g_free(cache);
	g_free(output);
	g_strfreev(parts);
	g_free(escaped);
	g_free(normalized);
	g_free(contents);
	return ok;
}
#endif

bool runtime_init(void) {
	char* prefix = installation_prefix();
	if (!prefix) {
		return true;
	}
	char*
	     marker = g_build_filename(prefix, "share", "crsign", "portable", NULL);
	bool packaged = g_file_test(marker, G_FILE_TEST_IS_REGULAR);
	g_free(marker);
	bool ok = true;
#ifdef SIGN_WITH_GUI
	if (packaged) {
		char* schemas = g_build_filename(
		    prefix,
		    "share",
		    "glib-2.0",
		    "schemas",
		    NULL
		);
		char* fonts = g_build_filename(
		    prefix,
		    "etc",
		    "fonts",
		    "fonts.conf",
		    NULL
		);
		char* gio = g_build_filename(prefix, "lib", "gio", "modules", NULL);
		char* share = g_build_filename(prefix, "share", NULL);
		const char* configured = g_getenv("XDG_DATA_DIRS");
		char*       existing = configured ? g_strdup(configured)
		                                  : g_strjoinv(
                                          G_SEARCHPATH_SEPARATOR_S,
                                          (char**)g_get_system_data_dirs()
                                      );
		char*
		    data = g_strconcat(share, G_SEARCHPATH_SEPARATOR_S, existing, NULL);
		resource_env("GSETTINGS_SCHEMA_DIR", schemas);
		resource_env("FONTCONFIG_FILE", fonts);
		resource_env("GIO_EXTRA_MODULES", gio);
		g_setenv("XDG_DATA_DIRS", data, true);
		ok = pixbuf_cache(prefix);
		g_free(data);
		g_free(existing);
		g_free(share);
		g_free(gio);
		g_free(fonts);
		g_free(schemas);
	}
#else
	(void)packaged;
#endif
	if (!ok) {
		fputs("sign: cannot initialize packaged image loaders\n", stderr);
	}
	g_free(prefix);
	return ok;
}
