/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <gio/gio.h>
#include <sign/certs/certs.h>
#include <sign/trust.h>
#include <stdio.h>
#include <stdlib.h>

bool trust_load_full(
    const sign_options* args,
    sign_trust*         trust,
    GError**            error
) {
	char detail[512];
	if (!file_load(args->ca_file, &trust->ca, detail, sizeof(detail)) ||
	    !file_load(
	        args->intermediates,
	        &trust->intermediates,
	        detail,
	        sizeof(detail)
	    ) ||
	    !file_load(args->tsa_ca_file, &trust->tsa_ca, detail, sizeof(detail)) ||
	    !file_load(
	        args->tsa_intermediates,
	        &trust->tsa_intermediates,
	        detail,
	        sizeof(detail)
	    )) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, detail);
		return false;
	}
	if (!certs_load(args, trust)) {
		g_set_error_literal(
		    error,
		    G_IO_ERROR,
		    G_IO_ERROR_FAILED,
		    "Cannot load certificate bundles"
		);
		return false;
	}
	return true;
}
bool trust_load(const sign_options* args, sign_trust* trust) {
	GError* error = NULL;
	bool    ok = trust_load_full(args, trust, &error);
	if (!ok) {
		fprintf(stderr, "sign: %s\n", error->message);
	}
	g_clear_error(&error);
	return ok;
}
void trust_free(sign_trust* trust) {
	free(trust->fallback_ca.data);
	free(trust->tsa_fallback_ca.data);
	free(trust->ca.data);
	free(trust->intermediates.data);
	free(trust->tsa_ca.data);
	free(trust->tsa_intermediates.data);
	*trust = (sign_trust){0};
}
void trust_verify_params(
    const sign_trust*      trust,
    libssig_verify_params* params
) {
	params->options |= LIBSSIG_VERIFY_SYSTEM_TRUST |
	                   LIBSSIG_VERIFY_TSA_SYSTEM_TRUST;
	params->fallback_ca_data = trust->fallback_ca.data;
	params->fallback_ca_data_len = trust->fallback_ca.len;
	params->tsa_fallback_ca_data = trust->tsa_fallback_ca.data;
	params->tsa_fallback_ca_data_len = trust->tsa_fallback_ca.len;
	params->ca_data = trust->ca.data;
	params->ca_data_len = trust->ca.len;
	params->intermediates_data = trust->intermediates.data;
	params->intermediates_data_len = trust->intermediates.len;
	params->tsa_ca_data = trust->tsa_ca.data;
	params->tsa_ca_data_len = trust->tsa_ca.len;
	params->tsa_intermediates_data = trust->tsa_intermediates.data;
	params->tsa_intermediates_data_len = trust->tsa_intermediates.len;
}
void trust_timestamp_params(
    const sign_trust*         trust,
    libssig_timestamp_params* params
) {
	params->options |= LIBSSIG_TIMESTAMP_SYSTEM_TRUST;
	params->fallback_ca_data = trust->tsa_fallback_ca.data;
	params->fallback_ca_data_len = trust->tsa_fallback_ca.len;
	params->ca_data = trust->tsa_ca.data;
	params->ca_data_len = trust->tsa_ca.len;
	params->intermediates_data = trust->tsa_intermediates.data;
	params->intermediates_data_len = trust->tsa_intermediates.len;
}
