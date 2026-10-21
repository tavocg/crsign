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
			json_string(levels[args->level]);
			fputs(",\"format\":", stdout);
			const char* formats[] = {"cades", "cms", "pades", "xades", "jades"};
			json_string(formats[args->format]);
			fputs(",\"output\":", stdout);
			json_string(args->output);
			printf(
			    ",\"len\":%zu,\"detached\":%s,\"timestamped\":%s",
			    len,
			    json_bool(args->detached),
			    json_bool(args->level != LIBSSIG_LEVEL_B_B)
			);
		} else {
			fputs(",\"error\":\"Operation failed; see stderr\"", stdout);
		}
		puts("}");
	} else if (ok && args->command == SIGN_COMMAND_SIGN) {
		printf("Signed: %s (%zu bytes)\n", args->output, len);
	}
}
