/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/arg.h>
#include <sign/json.h>
#include <stdio.h>

int main(int argc, char** argv) {
	sign_args args = arg_parse(argc, argv);
	printf(
	    "{\"algorithm\":%d,\"hash\":%d,\"level\":%d,\"format\":%d,"
	    "\"format_explicit\":%s,\"detached\":%s,\"no_bundled_certs\":%s",
	    args.algo,
	    args.hash,
	    args.level,
	    args.format,
	    json_bool(args.format_explicit),
	    json_bool(args.detached),
	    json_bool(args.no_bundled_certs)
	);
#define STRING_FIELD(name, member)                                             \
	fputs(",\"" name "\":", stdout);                                           \
	json_string(args.member)
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
	fputs(",\"cert_bundles\":[", stdout);
	for (size_t i = 0; i < args.cert_bundle_count; ++i) {
		if (i) {
			putchar(',');
		}
		json_string(args.cert_bundles[i]);
	}
	puts("]}");
	arg_free(&args);
	return 0;
}
