/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <glib.h>
#include <sign/runtime.h>
#include <stdio.h>
int main(void) {
	if (!runtime_init()) {
		return 1;
	}
	const char* names[] = {
	    "GSETTINGS_SCHEMA_DIR",
	    "FONTCONFIG_FILE",
	    "GIO_EXTRA_MODULES",
	    "XDG_DATA_DIRS",
	    "GDK_PIXBUF_MODULE_FILE",
	    "OPENSSL_MODULES"
	};
	for (size_t i = 0; i < G_N_ELEMENTS(names); ++i) {
		const char* value = g_getenv(names[i]);
		printf("%s=%s\n", names[i], value ? value : "");
	}
	return 0;
}
