/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/arg.h>
#include <sign/json.h>
#include <stdio.h>

int main(int argc, char** argv) {
	sign_args args = arg_parse(argc, argv);
	printf(
	    "{\"algorithm\":%d,\"hash\":%d,\"level\":%d,\"format\":%d,"
	    "\"format_explicit\":%s,\"detached\":%s,\"no_bundled_certs\":%s",
	    args.options.algo,
	    args.options.hash,
	    args.options.level,
	    args.options.format,
	    json_bool(args.options.format_explicit),
	    json_bool(args.options.detached),
	    json_bool(args.options.no_bundled_certs)
	);
#define STRING_FIELD(name, member)                                             \
	fputs(",\"" name "\":", stdout);                                           \
	json_print_string(args.options.member)
	STRING_FIELD("signature_image", appearance_file);
	STRING_FIELD("input", input);
	STRING_FIELD("token", uri);
	STRING_FIELD("cert_id", id);
	STRING_FIELD("authority", authority);
	STRING_FIELD("ca", ca_file);
	STRING_FIELD("intermediates", intermediates);
	STRING_FIELD("tsa_ca", tsa_ca_file);
	STRING_FIELD("tsa_intermediates", tsa_intermediates);
	STRING_FIELD("mime_type", mime_type);
	STRING_FIELD("output", output);
#undef STRING_FIELD
	printf(
	    ",\"page_number\":%u,\"x\":%g,\"y\":%g,\"width\":%g,\"height\":%g",
	    args.options.appearance.page_number,
	    args.options.appearance.x,
	    args.options.appearance.y,
	    args.options.appearance.width,
	    args.options.appearance.height
	);
	fputs(",\"modules\":[", stdout);
	for (size_t i = 0; i < args.options.modules.count; ++i) {
		if (i) {
			putchar(',');
		}
		json_print_string(args.options.modules.items[i]);
	}
	putchar(']');
	fputs(",\"pincmd\":[", stdout);
	for (size_t i = 0; args.pincmd && args.pincmd[i]; ++i) {
		if (i) {
			putchar(',');
		}
		json_print_string(args.pincmd[i]);
	}
	putchar(']');
	fputs(",\"cert_bundles\":[", stdout);
	for (size_t i = 0; i < args.options.cert_bundle_count; ++i) {
		if (i) {
			putchar(',');
		}
		json_print_string(args.options.cert_bundles[i]);
	}
	puts("]}");
	arg_free(&args);
	return 0;
}
