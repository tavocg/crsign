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
	g_assert_true(preferences_save(loaded, &error));
	g_assert_no_error(error);
	after = read_file(path);
	g_assert_cmpstr(after, ==, "version = 0.1\n");
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
