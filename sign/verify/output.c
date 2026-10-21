/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "internal.h"
#include <sign/json.h>
#include <stdio.h>

static const char* level_name(const libssig_verify_signer* signer) {
	if (!signer->level_detected) {
		return NULL;
	}
	const char* names[] = {"B-LTA", "B-B", "B-T", "B-LT"};
	return names[signer->level];
}

static const char* history_name(libssig_history_status status) {
	const char* names[] =
	    {"not-checked", "valid", "incomplete", "revoked", "untrusted"};
	return names[status];
}

static void verification_error(libssig_status status) {
	if (status == LIBSSIG_OK) {
		return;
	}
	const char* message = "verification failed";
	switch (status) {
	case LIBSSIG_ERROR_EVIDENCE:
		message = "validation incomplete: evidence is missing, stale or "
		          "unsupported";
		break;
	case LIBSSIG_ERROR_TRUST:
		message = "certificate or timestamp chain is not trusted or has "
		          "expired";
		break;
	case LIBSSIG_ERROR_REVOKED:
		message = "a certificate is revoked under the selected validation "
		          "policy";
		break;
	case LIBSSIG_ERROR_TIMESTAMP:
		message = "timestamp is invalid or required evidence is missing";
		break;
	case LIBSSIG_ERROR_CONTENT_REQUIRED:
		message = "detached signature requires the original content";
		break;
	default:
		break;
	}
	fprintf(stderr, "sign: %s (status=%d)\n", message, (int)status);
}

static void signer_profile_json(const libssig_verify_signer* signer) {
	fputs(",\"level\":", stdout);
	json_string(level_name(signer));
	printf(
	    ",\"level_detected\":%s,\"level_validated\":%s,\"history\":",
	    json_bool(signer->level_detected),
	    json_bool(signer->level_validated)
	);
	json_string(history_name(signer->history));
	fputs(",\"validation_time\":", stdout);
	json_string(signer->validation_time[0] ? signer->validation_time : NULL);
	printf(",\"revocation_checked\":%s", json_bool(signer->revocation_checked));
}

static void signer_profile_text(const libssig_verify_signer* signer, size_t i) {
	const char* level = level_name(signer);
	printf(
	    "Signer %zu: %s (%s).\n",
	    i + 1,
	    level ? level : "no baseline level detected",
	    signer->level_validated ? "validated" : "not validated"
	);
	if (signer->history != LIBSSIG_HISTORY_NOT_CHECKED) {
		printf("Historical validation: %s", history_name(signer->history));
		if (signer->validation_time[0]) {
			printf(" at %s", signer->validation_time);
		}
		puts(".");
	}
}

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
		fputs(",\"archive\":", stdout);
		timestamp_json(&signer->archive);
		signer_profile_json(signer);
		putchar('}');
	}
	fputs("],\"format\":", stdout);
	const char* formats[] = {"cades", "cms", "pades"};
	json_string(result->count ? formats[result->format] : NULL);
	puts("}");
}

void verify_output(
    const sign_args*             args,
    libssig_status               status,
    const libssig_verify_result* result
) {
	verification_error(status);
	if (args->json) {
		verify_json(status, result);
		return;
	}

	if (status != LIBSSIG_OK) {
		for (size_t i = 0; i < result->count; ++i) {
			signer_profile_text(&result->signers[i], i);
		}
		return;
	}
	printf(
	    "Signature and certificate chain valid (%zu signer(s)).\n",
	    result->count
	);
	for (size_t i = 0; i < result->count; ++i) {
		signer_profile_text(&result->signers[i], i);
		if (result->signers[i].timestamp.present) {
			printf(
			    "Timestamp valid and trusted: %s\n",
			    result->signers[i].timestamp.time
			);
		}
		if (result->signers[i].archive.present) {
			printf(
			    "Archive timestamp valid and trusted: %s\n",
			    result->signers[i].archive.time
			);
		}
	}
}
