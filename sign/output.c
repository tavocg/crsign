/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/json.h>
#include <sign/output.h>
#include <stdio.h>

void operation_output(const sign_args* args, bool ok, size_t len) {
	const char* command = args->command == SIGN_COMMAND_VERIFY ? "verify"
	                                                           : "sign";
	if (args->json) {
		printf("{\"command\":\"%s\",\"ok\":%s", command, json_bool(ok));
		if (ok) {
			const char* levels[] = {"B-LTA", "B-B", "B-T", "B-LT"};
			fputs(",\"level\":", stdout);
			json_print_string(levels[args->options.level]);
			fputs(",\"format\":", stdout);
			const char* formats[] = {"cades", "cms", "pades", "xades", "jades"};
			json_print_string(formats[args->options.format]);
			fputs(",\"output\":", stdout);
			json_print_string(args->options.output);
			printf(
			    ",\"len\":%zu,\"detached\":%s,\"timestamped\":%s",
			    len,
			    json_bool(args->options.detached),
			    json_bool(args->options.level != LIBSSIG_LEVEL_B_B)
			);
		} else {
			fputs(",\"error\":\"Operation failed; see stderr\"", stdout);
		}
		puts("}");
	} else if (ok && args->command == SIGN_COMMAND_SIGN) {
		printf("Signed: %s (%zu bytes)\n", args->options.output, len);
	}
}
