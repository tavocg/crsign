/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <gio/gio.h>
#include <sign/runtime.h>

int main(void) {
	if (!runtime_init()) {
		return 1;
	}
	GSettingsSchemaSource* source = g_settings_schema_source_get_default();
	if (!source) {
		return 2;
	}
	GSettingsSchema* schema = g_settings_schema_source_lookup(
	    source,
	    "org.gtk.gtk4.Settings.FileChooser",
	    TRUE
	);
	if (!schema) {
		return 3;
	}
	g_settings_schema_unref(schema);
	return 0;
}
