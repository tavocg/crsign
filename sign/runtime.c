/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif
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
#if defined(SIGN_WITH_GUI) && !defined(G_OS_WIN32)
#include <dlfcn.h>
/* Weak so headless test drivers can link runtime.c without GTK. */
extern void gtk_init(void) __attribute__((weak));
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

#if defined(SIGN_WITH_GUI) && !defined(G_OS_WIN32)
/* GTK's internal file chooser loads org.gtk.gtk4.Settings.FileChooser. A bare
 * build or a symlinked executable may not have GTK's schema directory in
 * XDG_DATA_DIRS, so find it next to the linked library before GTK starts
 * GSettings. */
static char* gtk_schema_data_dir(void) {
	if (!gtk_init) {
		return NULL;
	}
	union {
		void (*function)(void);
		void* object;
	} symbol = {.function = gtk_init};
	Dl_info info;
	if (!dladdr(symbol.object, &info) || !info.dli_fname) {
		return NULL;
	}
	char* library = g_path_get_dirname(info.dli_fname);
	char* prefix = g_path_get_dirname(library);
	g_free(library);
	char* base = g_build_filename(prefix, "share", "gsettings-schemas", NULL);
	char* result = NULL;
	GDir* dir = g_dir_open(base, 0, NULL);
	if (dir) {
		const char* name;
		while ((name = g_dir_read_name(dir))) {
			if (!g_str_has_prefix(name, "gtk4-")) {
				continue;
			}
			char* candidate = g_build_filename(base, name, NULL);
			char* compiled = g_build_filename(
			    candidate,
			    "glib-2.0",
			    "schemas",
			    "gschemas.compiled",
			    NULL
			);
			if (g_file_test(compiled, G_FILE_TEST_IS_REGULAR)) {
				result = candidate;
				g_free(compiled);
				break;
			}
			g_free(compiled);
			g_free(candidate);
		}
		g_dir_close(dir);
	}
	g_free(base);
	if (!result) {
		char* share = g_build_filename(prefix, "share", NULL);
		char* compiled = g_build_filename(
		    share,
		    "glib-2.0",
		    "schemas",
		    "gschemas.compiled",
		    NULL
		);
		if (g_file_test(compiled, G_FILE_TEST_IS_REGULAR)) {
			result = share;
		} else {
			g_free(share);
		}
		g_free(compiled);
	}
	g_free(prefix);
	return result;
}
static void ensure_gtk_schema_data_dir(void) {
	char* data = gtk_schema_data_dir();
	if (!data) {
		return;
	}
	const char* configured = g_getenv("XDG_DATA_DIRS");
	char*       existing = configured ? g_strdup(configured)
	                                  : g_strdup("/usr/local/share/:/usr/share/");
	char* value = g_strconcat(data, G_SEARCHPATH_SEPARATOR_S, existing, NULL);
	g_setenv("XDG_DATA_DIRS", value, true);
	g_free(value);
	g_free(existing);
	g_free(data);
}
#else
static void ensure_gtk_schema_data_dir(void) {
}
#endif
bool runtime_init(void) {
	ensure_gtk_schema_data_dir();
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
