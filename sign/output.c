/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/json.h>
#include <sign/output.h>
#include <stdio.h>

static void timestamp_json(const libssig_verify_timestamp* stamp) {
	printf(
	    "{\"present\":%s,\"valid\":%s,\"trust_checked\":%s,\"trusted\":%s,"
	    "\"status\":%d,\"trust_error\":%ld,\"time\":",
	    json_bool(stamp->present),
	    json_bool(stamp->valid),
	    json_bool(stamp->trust_checked),
	    json_bool(stamp->trusted),
	    (int)stamp->status,
	    stamp->trust_error
	);
	json_string(stamp->time[0] ? stamp->time : NULL);
	putchar('}');
}

static void verify_json(
    libssig_status               status,
    const libssig_verify_result* result
) {
	printf(
	    "{\"command\":\"verify\",\"ok\":%s,\"status\":%d,\"signature_valid\":%"
	    "s,\"trust_checked\":%s,\"trusted\":%s,\"detached\":%s,\"count\":%zu,"
	    "\"signers\":[",
	    json_bool(status == LIBSSIG_OK),
	    (int)status,
	    json_bool(result->signature_valid),
	    json_bool(result->trust_checked),
	    json_bool(result->trusted),
	    json_bool(result->detached),
	    result->count
	);
	for (size_t i = 0; i < result->count; ++i) {
		const libssig_verify_signer* signer = &result->signers[i];
		if (i) {
			putchar(',');
		}
		printf("{\"trust_error\":%ld,\"mime_type\":", signer->trust_error);
		json_string(signer->mime_type);
		fputs(",\"certificate_der_hex\":", stdout);
		json_hex(signer->cert_der, signer->cert_der_len);
		fputs(",\"timestamp\":", stdout);
		timestamp_json(&signer->timestamp);
		putchar('}');
	}
	puts("]}");
}

void verify_output(
    const sign_args*             args,
    libssig_status               status,
    const libssig_verify_result* result
) {
	if (args->json) {
		verify_json(status, result);
		return;
	}

	if (status != LIBSSIG_OK) {
		return;
	}
	printf(
	    "Signature and certificate chain valid (%zu signer(s)).\n",
	    result->count
	);
	for (size_t i = 0; i < result->count; ++i) {
		if (result->signers[i].timestamp.present) {
			printf(
			    "Timestamp valid and trusted: %s\n",
			    result->signers[i].timestamp.time
			);
		}
	}
}

void operation_output(const sign_args* args, bool ok, size_t len) {
	const char* command = args->command == SIGN_COMMAND_VERIFY ? "verify"
	                                                           : "sign";
	if (args->json) {
		printf("{\"command\":\"%s\",\"ok\":%s", command, json_bool(ok));
		if (ok) {
			fputs(",\"output\":", stdout);
			json_string(args->output);
			printf(
			    ",\"len\":%zu,\"detached\":%s,\"timestamped\":%s",
			    len,
			    json_bool(args->detached),
			    json_bool(args->authority != NULL)
			);
		} else {
			fputs(",\"error\":\"Operation failed; see stderr\"", stdout);
		}
		puts("}");
	} else if (ok && args->command == SIGN_COMMAND_SIGN) {
		printf("Signed: %s (%zu bytes)\n", args->output, len);
	}
}
