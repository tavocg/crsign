/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/history.h>
#include <sign/ui_preferences.h>
#include <stdio.h>
#include <string.h>
#ifdef G_OS_WIN32
#include <glib/gwin32.h>
#endif
static int run(int argc, char** argv) {
	if (argc < 2) {
		return 2;
	}
	GError* error = NULL;
	bool    ok = false;
	if (!strcmp(argv[1], "view")) {
		bool grid = true;
		ok = ui_preferences_grid(&grid, &error);
		if (ok) {
			puts(grid ? "grid" : "list");
		}
	} else if (!strcmp(argv[1], "set-view") && argc == 3) {
		ok = ui_preferences_set_grid(!strcmp(argv[2], "grid"), &error);
	} else if (!strcmp(argv[1], "hardware-only")) {
		bool hardware_only = true;
		ok = ui_preferences_hardware_only(&hardware_only, &error);
		if (ok) {
			puts(hardware_only ? "true" : "false");
		}
	} else if (!strcmp(argv[1], "set-hardware-only") && argc == 3) {
		ok = ui_preferences_set_hardware_only(!strcmp(argv[2], "true"), &error);
	} else if (!strcmp(argv[1], "record") && argc == 3) {
		ok = history_record(argv[2], &error);
	} else if (!strcmp(argv[1], "favorite") && argc == 4) {
		ok = history_favorite(argv[2], !strcmp(argv[3], "true"), &error);
	} else if (!strcmp(argv[1], "load")) {
		sign_history* history = history_load(&error);
		ok = history != NULL;
		if (history) {
			printf("%zu\n", history_count(history));
			history_free(history);
		}
	}
	if (error) {
		fprintf(stderr, "%s\n", error->message);
		g_clear_error(&error);
	}
	return ok ? 0 : 1;
}

int main(int argc, char** argv) {
#ifdef G_OS_WIN32
	/* Match the production CLI: GLib paths are UTF-8, not the CRT code page. */
	char** utf8_argv = g_win32_get_command_line();
	argc = (int)g_strv_length(utf8_argv);
	argv = utf8_argv;
#endif
	int status = run(argc, argv);
#ifdef G_OS_WIN32
	g_strfreev(utf8_argv);
#endif
	return status;
}
