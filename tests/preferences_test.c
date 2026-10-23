/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <glib/gstdio.h>
#include <sign/config.h>
#include <sign/preferences.h>
#include <string.h>

static void text(
    sign_preferences_section* section,
    const char*               key,
    const char*               value
) {
	preferences_set(section, key, g_variant_new_string(value));
}
static char* read_file(const char* path) {
	char* source = NULL;
	g_assert_true(g_file_get_contents(path, &source, NULL, NULL));
	return source;
}
int main(void) {
	GError* error = NULL;
	char*   directory = g_dir_make_tmp("crsign-preferences-XXXXXX", &error);
	g_assert_no_error(error);
	g_setenv("XDG_CONFIG_HOME", directory, TRUE);
	char*             path = config_path();
	char*             parent = g_path_get_dirname(path);
	sign_preferences* p = preferences_load(&error);
	g_assert_no_error(error);
	g_assert_nonnull(p);
	g_assert_false(g_file_test(parent, G_FILE_TEST_EXISTS));
	g_assert_cmpuint(preferences_count(p), ==, 0);
	sign_preferences*         stale_missing = preferences_load(&error);
	sign_preferences_section* global = preferences_global(p);
	text(global, "token", "pkcs11:token=Global");
	text(global, "hash", "sha384");
	text(global, "signature_image", "images/default.svg");
	text(global, "tsa.ca", "certs/tsa.pem");
	const char* modules[] = {"modules/one.so", "modules/two.so"};
	preferences_set(global, "modules", g_variant_new_strv(modules, 2));
	const char* command[] =
	    {"pin-provider", "a quoted \"argument\"", "", "line\nnext"};
	preferences_set(global, "pincmd", g_variant_new_strv(command, 4));
	sign_preferences_section* profile = preferences_add(p);
	text(profile, "label", "bccr");
	text(profile, "name", "Banco ñ \"quoted\"\nsecond\tline\\end");
	text(profile, "hash", "sha512");
	preferences_set(profile, "modules", g_variant_new_strv(NULL, 0));
	preferences_set(profile, "pincmd", g_variant_new_strv(NULL, 0));
	g_assert_true(preferences_save(p, &error));
	g_assert_no_error(error);
	g_assert_true(g_file_test(path, G_FILE_TEST_IS_REGULAR));
	g_assert_false(preferences_save(stale_missing, &error));
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_WRONG_ETAG);
	g_clear_error(&error);
	preferences_free(stale_missing);
	sign_preferences* loaded = preferences_load(&error);
	g_assert_no_error(error);
	g_assert_cmpuint(preferences_count(loaded), ==, 1);
	g_assert_true(g_variant_equal(
	    preferences_get(profile, "name"),
	    preferences_get(preferences_profile(loaded, 0), "name")
	));
	g_assert_true(g_variant_equal(
	    preferences_get(global, "pincmd"),
	    preferences_get(preferences_global(loaded), "pincmd")
	));
	char      detail[512];
	sign_args args = {0};
	g_assert_true(config_load(&args, NULL, detail, sizeof(detail)));
	g_assert_cmpint(args.options.hash, ==, LIBSSIG_HASH_SHA384);
	g_assert_cmpstr(args.options.uri, ==, "pkcs11:token=Global");
	g_assert_cmpuint(args.options.modules.count, ==, 2);
	g_assert_cmpstr(args.pincmd[2], ==, "");
	g_assert_cmpstr(args.pincmd[3], ==, "line\nnext");
	g_assert_cmpuint(args.options.appearance_options, ==, 0);
	arg_free(&args);
	g_assert_true(config_load(&args, "bccr", detail, sizeof(detail)));
	g_assert_cmpint(args.options.hash, ==, LIBSSIG_HASH_SHA512);
	g_assert_cmpstr(args.options.uri, ==, "pkcs11:token=Global");
	g_assert_cmpuint(args.options.modules.count, ==, 0);
	g_assert_null(args.pincmd);
	arg_free(&args);
	/* Empty values bypass non-default globals and survive every round trip. */
	text(global, "algorithm", "rsa-pss");
	text(global, "level", "B-B");
	text(global, "format", "xades");
	text(global, "ca", "root.pem");
	preferences_set(global, "detached", g_variant_new_boolean(true));
	preferences_set(global, "no_bundled_certs", g_variant_new_boolean(true));
	const char* automatic[] = {
	    "token",
	    "cert_id",
	    "algorithm",
	    "hash",
	    "level",
	    "format",
	    "ca",
	    "intermediates",
	    "tsa.url",
	    "tsa.ca",
	    "tsa.intermediates",
	    "signature_image",
	    "detached",
	    "no_bundled_certs"
	};
	for (guint i = 0; i < G_N_ELEMENTS(automatic); ++i) {
		text(profile, automatic[i], "");
	}
	g_assert_true(preferences_save(p, &error));
	g_assert_no_error(error);
	g_assert_true(config_load(&args, "bccr", detail, sizeof(detail)));
	g_assert_null(args.options.uri);
	g_assert_null(args.options.id);
	g_assert_null(args.options.ca_file);
	g_assert_null(args.options.tsa_ca_file);
	g_assert_null(args.options.appearance_file);
	g_assert_cmpint(args.options.algo, ==, LIBSSIG_ALGO_RSA_PKCS);
	g_assert_cmpint(args.options.hash, ==, LIBSSIG_HASH_SHA256);
	g_assert_cmpint(args.options.level, ==, LIBSSIG_LEVEL_B_LTA);
	g_assert_false(args.options.format_explicit);
	g_assert_false(args.options.detached);
	g_assert_false(args.options.no_bundled_certs);
	arg_free(&args);
	sign_preferences* roundtrip = preferences_load(&error);
	g_assert_no_error(error);
	for (guint i = 0; i < G_N_ELEMENTS(automatic); ++i) {
		GVariant* value = preferences_get(
		    preferences_profile(roundtrip, 0),
		    automatic[i]
		);
		g_assert_nonnull(value);
		g_assert_cmpstr(g_variant_get_string(value, NULL), ==, "");
	}
	g_assert_true(preferences_save(roundtrip, &error));
	g_assert_no_error(error);
	preferences_free(roundtrip);
	/* Reload p after another writer's successful round trip. */
	preferences_free(p);
	p = preferences_load(&error);
	g_assert_no_error(error);
	profile = preferences_profile(p, 0);
	char* before = read_file(path);
	text(profile, "hash", "unsupported");
	g_assert_false(preferences_save(p, &error));
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
	g_clear_error(&error);
	char* after = read_file(path);
	g_assert_cmpstr(before, ==, after);
	g_free(after);
	preferences_set(profile, "hash", NULL);
	text(profile, "label", "");
	g_assert_false(preferences_save(p, &error));
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
	g_clear_error(&error);
	text(profile, "label", "bccr");
	sign_preferences_section* duplicate = preferences_add(p);
	text(duplicate, "label", "bccr");
	g_assert_false(preferences_save(p, &error));
	g_clear_error(&error);
	preferences_remove(p, 1);
	g_assert_true(preferences_save(p, &error));
	g_assert_no_error(error);
	/* Existing snapshots also reject external edits and preserve their bytes.
	 */
	g_usleep(20000);
	g_assert_true(g_file_set_contents(
	    path,
	    "version = 0.1\nhash = \"sha256\"\n",
	    -1,
	    &error
	));
	g_assert_false(preferences_save(loaded, &error));
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_WRONG_ETAG);
	g_clear_error(&error);
	after = read_file(path);
	g_assert_cmpstr(after, ==, "version = 0.1\nhash = \"sha256\"\n");
	g_free(after);
	preferences_free(loaded);
	preferences_free(p);
	loaded = preferences_load(&error);
	g_assert_no_error(error);
	text(preferences_global(loaded), "hash", "");
	text(preferences_global(loaded), "tsa.ca", "");
	preferences_set(
	    preferences_global(loaded),
	    "modules",
	    g_variant_new_strv(NULL, 0)
	);
	preferences_set(
	    preferences_global(loaded),
	    "pincmd",
	    g_variant_new_strv(NULL, 0)
	);
	g_assert_true(preferences_save(loaded, &error));
	g_assert_no_error(error);
	after = read_file(path);
	g_assert_cmpstr(after, ==, "version = 0.1\n");
	g_assert_null(preferences_get(preferences_global(loaded), "hash"));
	g_assert_null(preferences_get(preferences_global(loaded), "tsa.ca"));
	g_assert_null(preferences_get(preferences_global(loaded), "modules"));
	g_assert_null(preferences_get(preferences_global(loaded), "pincmd"));
	/* Booleans are values, not empty settings. Profile empties remain explicit.
	 */
	preferences_set(
	    preferences_global(loaded),
	    "detached",
	    g_variant_new_boolean(false)
	);
	sign_preferences_section* automatic_profile = preferences_add(loaded);
	text(automatic_profile, "label", "automatic");
	text(automatic_profile, "hash", "");
	preferences_set(automatic_profile, "modules", g_variant_new_strv(NULL, 0));
	g_assert_true(preferences_save(loaded, &error));
	g_assert_no_error(error);
	sign_preferences* cleaned = preferences_load(&error);
	g_assert_no_error(error);
	g_assert_false(g_variant_get_boolean(
	    preferences_get(preferences_global(cleaned), "detached")
	));
	g_assert_cmpstr(
	    g_variant_get_string(
	        preferences_get(preferences_profile(cleaned, 0), "hash"),
	        NULL
	    ),
	    ==,
	    ""
	);
	g_assert_cmpuint(
	    g_variant_n_children(
	        preferences_get(preferences_profile(cleaned, 0), "modules")
	    ),
	    ==,
	    0
	);
	preferences_free(cleaned);
	g_free(after);
	preferences_free(loaded);
	const char* invalid[] = {
	    "version = 0.2",
	    "bad toml",
	    "version = 0.1\nprofiles = [1]",
	    "version = 0.1\nunknown = true",
	    "version = 0.1\n[[profiles]]\nlabel = \"p\"\nhash = \"bad\""
	};
	for (guint i = 0; i < G_N_ELEMENTS(invalid); ++i) {
		g_assert_true(g_file_set_contents(path, invalid[i], -1, &error));
		g_assert_null(preferences_load(&error));
		g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
		g_clear_error(&error);
		after = read_file(path);
		g_assert_cmpstr(after, ==, invalid[i]);
		g_free(after);
	}
	g_free(before);
	g_assert_cmpint(g_unlink(path), ==, 0);
	g_assert_cmpint(g_rmdir(parent), ==, 0);
	g_assert_cmpint(g_rmdir(directory), ==, 0);
	g_free(path);
	g_free(parent);
	g_free(directory);
	return 0;
}
