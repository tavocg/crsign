/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <sign/modules.h>
#include <string.h>

static void inventory(libssig_context* context) {
	libssig_device_list devices = {0};
	assert(
	    libssig_devices(context, &devices, LIBSSIG_DEVICES_ALL) == LIBSSIG_OK
	);
	bool embedded = false, vendor = false;
	for (size_t n = 0; n < devices.count; ++n) {
		libssig_device_module* module = &devices.items[n];
		if (module->description && strstr(module->description, "OpenSC")) {
			embedded |= !module->path;
		}
		if (module->description && !strcmp(module->description, "Sign-first")) {
			vendor = true;
		}
	}
#ifdef SIGN_EMBED_OPENSC
	assert(embedded && vendor);
#else
	assert(!embedded && vendor);
#endif
	libssig_devices_free(&devices);
}

int main(void) {
	sign_options    options = {0};
	libssig_context first = {0}, second = {0};
	assert(modules_open(&options, &first) == LIBSSIG_OK);
	assert(modules_open(&options, &second) == LIBSSIG_OK);
	inventory(&first);
	libssig_context_close(&first);
	/* Closing a signing context must leave inventory/other contexts usable. */
	inventory(&second);
	libssig_context_close(&second);
	return 0;
}
