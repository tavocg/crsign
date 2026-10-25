/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Linux-only opt-in observer; build separately with the driver's headers.
 * Interpose only signing stages. Never inspect PINs, APDUs or payloads. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <libopensc/opensc.h>
#include <libopensc/pkcs15.h>
#include <link.h>
#include <stdio.h>
#include <string.h>

static __thread unsigned signing;

typedef struct {
	const char* operation;
	void*       symbol;
} lookup;

static int loaded_library(struct dl_phdr_info* info, size_t size, void* data) {
	(void)size;
	lookup*     search = data;
	const char* name = strrchr(info->dlpi_name, '/');
	name = name ? name + 1 : info->dlpi_name;
	const char prefix[] = "libopensc.so";
	size_t     length = sizeof(prefix) - 1;
	if (strncmp(name, prefix, length) ||
	    (name[length] && name[length] != '.')) {
		return 0;
	}
	/* p11-kit normally loads modules locally. RTLD_NEXT from this global
	 * observer cannot see that local dependency. Obtain an existing handle;
	 * never load or initialize another copy of the driver. */
	void* handle = dlopen(info->dlpi_name, RTLD_LAZY | RTLD_NOLOAD);
	if (handle) {
		search->symbol = dlsym(handle, search->operation);
		dlclose(handle);
	}
	return search->symbol != NULL;
}

static void* resolve(const char* operation) {
	void* symbol = dlsym(RTLD_NEXT, operation);
	if (!symbol) {
		lookup search = {.operation = operation};
		dl_iterate_phdr(loaded_library, &search);
		symbol = search.symbol;
	}
	return symbol;
}

static void begin(const char* operation) {
	fprintf(stderr, "opensc-sign: operation=%s phase=begin\n", operation);
}
static void end(const char* operation, int result) {
	fprintf(
	    stderr,
	    "opensc-sign: operation=%s phase=end result=%d\n",
	    operation,
	    result
	);
}

/* POSIX permits converting a dlsym result to a function pointer. memcpy also
 * keeps the compiler's function-pointer conversion diagnostics useful. */
#define RESOLVE(operation, type)                                               \
	void* symbol = resolve(#operation);                                        \
	type  real = NULL;                                                         \
	memcpy(&real, &symbol, sizeof(real));                                      \
	if (!real) {                                                               \
		end(#operation, SC_ERROR_INTERNAL);                                    \
		return SC_ERROR_INTERNAL;                                              \
	}

int sc_pkcs15_compute_signature(
    struct sc_pkcs15_card*         card,
    const struct sc_pkcs15_object* key,
    unsigned long                  flags,
    const u8*                      input,
    size_t                         input_len,
    u8*                            output,
    size_t                         output_len,
    void*                          mechanism
) {
	typedef int (*function)(
	    struct sc_pkcs15_card*,
	    const struct sc_pkcs15_object*,
	    unsigned long,
	    const u8*,
	    size_t,
	    u8*,
	    size_t,
	    void*
	);
	RESOLVE(sc_pkcs15_compute_signature, function);
	++signing;
	begin("sc_pkcs15_compute_signature");
	int result = real(
	    card,
	    key,
	    flags,
	    input,
	    input_len,
	    output,
	    output_len,
	    mechanism
	);
	end("sc_pkcs15_compute_signature", result);
	--signing;
	return result;
}

int sc_select_file(
    struct sc_card*  card,
    const sc_path_t* path,
    sc_file_t**      file
) {
	typedef int (*function)(struct sc_card*, const sc_path_t*, sc_file_t**);
	RESOLVE(sc_select_file, function);
	const char* operation = "sc_select_file(file)";
	if (path && (path->type == SC_PATH_TYPE_DF_NAME || path->aid.len)) {
		operation = "sc_select_file(application)";
	} else if (path && path->len == 2 && path->value[0] == 0x3f &&
	           path->value[1] == 0) {
		operation = "sc_select_file(root)";
	}
	if (signing) {
		begin(operation);
	}
	int result = real(card, path, file);
	if (signing) {
		end(operation, result);
	}
	return result;
}

int sc_set_security_env(
    struct sc_card*               card,
    const struct sc_security_env* environment,
    int                           number
) {
	typedef int (*function)(
	    struct sc_card*,
	    const struct sc_security_env*,
	    int
	);
	RESOLVE(sc_set_security_env, function);
	if (signing) {
		begin("sc_set_security_env");
	}
	int result = real(card, environment, number);
	if (signing) {
		end("sc_set_security_env", result);
	}
	return result;
}

int sc_compute_signature(
    struct sc_card* card,
    const u8*       input,
    size_t          input_len,
    u8*             output,
    size_t          output_len
) {
	typedef int (*function)(struct sc_card*, const u8*, size_t, u8*, size_t);
	RESOLVE(sc_compute_signature, function);
	if (signing) {
		begin("sc_compute_signature");
	}
	int result = real(card, input, input_len, output, output_len);
	if (signing) {
		end("sc_compute_signature", result);
	}
	return result;
}
