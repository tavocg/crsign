/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/trust/trust.h>
#include <stdlib.h>

bool trust_load(const sign_args* args, sign_trust* trust) {
	return file_read(args->ca_file, &trust->ca) &&
	       file_read(args->intermediates, &trust->intermediates) &&
	       file_read(args->tsa_ca_file, &trust->tsa_ca) &&
	       file_read(args->tsa_intermediates, &trust->tsa_intermediates);
}
void trust_free(sign_trust* trust) {
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
	params->ca_data = trust->tsa_ca.data;
	params->ca_data_len = trust->tsa_ca.len;
	params->intermediates_data = trust->tsa_intermediates.data;
	params->intermediates_data_len = trust->tsa_intermediates.len;
}
