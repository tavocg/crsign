/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Real OpenSSL-backed PKCS#11 signing behind the production flow executor. */
#include "../libssig/tests/document_sign_fixture.c"

static X509*                       flow_cert;
static unsigned char*              flow_der;
static int                         flow_der_len;
static CK_FUNCTION_LIST            flow_module;
static CK_FUNCTION_LIST*           flow_modules[] = {&flow_module, NULL};
static struct libssig_context_impl flow_impl;

libssig_status libssig_context_open(libssig_context* context) {
	if (!rsa) {
		rsa = EVP_PKEY_Q_keygen(NULL, NULL, "RSA", 2048);
		public_key = rsa;
		algorithm = LIBSSIG_ALGO_RSA_PKCS;
		key_type = CKK_RSA;
		digest = EVP_sha256();
		expected_mechanism = CKM_SHA256_RSA_PKCS;
		expected_hash = CKM_SHA256;
		expected_mgf = CKG_MGF1_SHA256;
		flow_cert = X509_new();
		if (!rsa || !flow_cert || !X509_set_version(flow_cert, 2) ||
		    !ASN1_INTEGER_set(X509_get_serialNumber(flow_cert), 10) ||
		    !X509_set_pubkey(flow_cert, rsa) ||
		    !X509_gmtime_adj(X509_getm_notBefore(flow_cert), -60) ||
		    !X509_gmtime_adj(X509_getm_notAfter(flow_cert), 3600) ||
		    !X509_NAME_add_entry_by_txt(
		        X509_get_subject_name(flow_cert),
		        "CN",
		        MBSTRING_ASC,
		        (const unsigned char*)"Flow signer",
		        -1,
		        -1,
		        0
		    ) ||
		    !X509_set_issuer_name(
		        flow_cert,
		        X509_get_subject_name(flow_cert)
		    ) ||
		    !X509_sign(flow_cert, rsa, EVP_sha256())) {
			return LIBSSIG_ERROR_CRYPTO;
		}
		flow_der_len = i2d_X509(flow_cert, &flow_der);
		if (flow_der_len <= 0) {
			return LIBSSIG_ERROR_CRYPTO;
		}
		flow_module.C_GetSlotList = mock_slots_get;
		flow_module.C_GetTokenInfo = mock_token_info_get;
		flow_module.C_OpenSession = mock_session_open;
		flow_module.C_CloseSession = mock_session_close;
		flow_module.C_Login = mock_session_login;
		flow_module.C_FindObjectsInit = mock_objects_find_init;
		flow_module.C_FindObjects = mock_objects_find;
		flow_module.C_FindObjectsFinal = mock_objects_find_final;
		flow_module.C_GetAttributeValue = mock_attributes_get;
		flow_module.C_SignInit = mock_sign_init;
		flow_module.C_Sign = document_raw_sign;
		flow_impl.modules = flow_modules;
	}
	context->impl = &flow_impl;
	return LIBSSIG_OK;
}
libssig_status libssig_context_open_modules(
    libssig_context*           context,
    const libssig_string_list* modules
) {
	(void)modules;
	return libssig_context_open(context);
}
void libssig_context_close(libssig_context* context) {
	context->impl = NULL;
}
libssig_status libssig_tokens(
    const libssig_context* context,
    libssig_token_list*    list,
    unsigned int           options
) {
	(void)context;
	(void)options;
	static const char* flags[] = {"login-required"};
	list->items = calloc(1, sizeof(*list->items));
	if (!list->items) {
		return LIBSSIG_ERROR_MEMORY;
	}
	list->count = 1;
	list->items[0] = (libssig_token){.uri = "pkcs11:token=test",
	                                 .flag_names = {flags, 1}};
	return LIBSSIG_OK;
}
void libssig_tokens_free(libssig_token_list* list) {
	free(list->items);
	*list = (libssig_token_list){0};
}
libssig_status libssig_certificates(
    const libssig_context*    context,
    const libssig_token*      token,
    libssig_certificate_list* list,
    unsigned int              options
) {
	(void)context;
	(void)token;
	(void)options;
	list->items = calloc(1, sizeof(*list->items));
	if (!list->items) {
		return LIBSSIG_ERROR_MEMORY;
	}
	list->count = 1;
	list->items[0] = (libssig_certificate){.id = (unsigned char*)id,
	                                       .id_len = sizeof(id),
	                                       .der = flow_der,
	                                       .der_len = (size_t)flow_der_len};
	return LIBSSIG_OK;
}
void libssig_certificates_free(libssig_certificate_list* list) {
	free(list->items);
	*list = (libssig_certificate_list){0};
}
