/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <libssig/trust_store.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <sign/certs/certs.h>
#include <sign/timestamp/timestamp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* All inputs are local. Discovery never downloads issuers or accesses a token.
 */
static bool certs_parse(const sign_buffer* buffer, STACK_OF(X509) * certs) {
	if (!buffer->len) {
		return true;
	}
	if (!buffer->data || buffer->len > 16u * 1024u * 1024u) {
		return false;
	}
	const unsigned char* cursor = buffer->data;
	X509*                cert = d2i_X509(NULL, &cursor, (long)buffer->len);
	if (cert) {
		if (cursor != buffer->data + buffer->len ||
		    sk_X509_num(certs) >= 1024 || !sk_X509_push(certs, cert)) {
			X509_free(cert);
			return false;
		}
		return true;
	}
	ERR_clear_error();
	BIO* bio = BIO_new_mem_buf(buffer->data, (int)buffer->len);
	if (!bio) {
		return false;
	}
	bool ok = true;
	while (BIO_ctrl_pending(bio)) {
		char c;
		if (BIO_read(bio, &c, 1) != 1) {
			ok = false;
			break;
		}
		if (strchr(" \t\r\n\f\v", c) && c) {
			continue;
		}
		if (BIO_seek(bio, BIO_tell(bio) - 1) < 0) {
			ok = false;
			break;
		}
		cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
		if (!cert || sk_X509_num(certs) >= 1024 || !sk_X509_push(certs, cert)) {
			X509_free(cert);
			ok = false;
			break;
		}
	}
	BIO_free(bio);
	ERR_clear_error();
	return ok;
}

static bool roots_add(X509_STORE* store, const sign_buffer* data) {
	STACK_OF(X509)* certs = sk_X509_new_null();
	bool ok = certs && certs_parse(data, certs);
	for (int i = 0; ok && i < sk_X509_num(certs); ++i) {
		ok = X509_STORE_add_cert(store, sk_X509_value(certs, i)) == 1;
	}
	sk_X509_pop_free(certs, X509_free);
	return ok;
}

static bool issuer_missing(int error) {
	return error == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT ||
	       error == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY ||
	       error == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT ||
	       error == X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN;
}

static STACK_OF(X509) *
    chain_verify(X509* cert, const sign_trust* trust, bool tsa) {
	const sign_buffer* ca = tsa ? &trust->tsa_ca : &trust->ca;
	const sign_buffer* inter = tsa ? &trust->tsa_intermediates
	                               : &trust->intermediates;
	const sign_buffer* fallback = tsa ? &trust->tsa_fallback_ca
	                                  : &trust->fallback_ca;
	STACK_OF(X509)* untrusted = sk_X509_new_null();
	STACK_OF(X509)* chain = NULL;
	X509_STORE*     store = X509_STORE_new();
	X509_STORE_CTX* ctx = X509_STORE_CTX_new();
	if (!untrusted || !store || !ctx || !certs_parse(inter, untrusted)) {
		goto done;
	}
	if (!roots_add(store, ca)) {
		goto done;
	}
	/* Discovery is best effort; explicit anchors can still supply a chain.
	 * The loader installs native policy checks before adding certificates. */
	libssig_trust_store_load_system(store);
	ERR_clear_error();
	for (int i = 0; i < 2; ++i) {
		if (!X509_STORE_CTX_init(ctx, store, cert, untrusted)) {
			break;
		}
		X509_STORE_CTX_set_purpose(
		    ctx,
		    tsa ? X509_PURPOSE_TIMESTAMP_SIGN : X509_PURPOSE_ANY
		);
		X509_VERIFY_PARAM_set_depth(X509_STORE_CTX_get0_param(ctx), 16);
		int ok = X509_verify_cert(ctx);
		int error = X509_STORE_CTX_get_error(ctx);
		if (ok == 1) {
			chain = X509_STORE_CTX_get1_chain(ctx);
		}
		X509_STORE_CTX_cleanup(ctx);
		if (chain || i || !issuer_missing(error) || !fallback->len ||
		    !roots_add(store, fallback)) {
			break;
		}
	}
done:
	X509_STORE_CTX_free(ctx);
	X509_STORE_free(store);
	sk_X509_pop_free(untrusted, X509_free);
	ERR_clear_error();
	return chain;
}

static bool url_valid(const unsigned char* data, int len) {
	if (len < 8 || len > 2048 ||
	    (memcmp(data, "http://", 7) &&
	     (len < 9 || memcmp(data, "https://", 8)))) {
		return false;
	}
	int host = data[4] == ':' ? 7 : 8;
	if (data[host] == '/' || data[host] == '?' || data[host] == '#') {
		return false;
	}
	for (int i = 0; i < len; ++i) {
		if (data[i] <= 32 || data[i] >= 127 || data[i] == '#' ||
		    data[i] == '@' || data[i] == '\\') {
			return false;
		}
	}
	return true;
}

static bool url_select(char** url, const char* candidate) {
	if (*url) {
		return !strcmp(*url, candidate);
	}
	size_t len = strlen(candidate);
	*url = malloc(len + 1);
	if (*url) {
		memcpy(*url, candidate, len + 1);
	}
	return *url != NULL;
}

/* SIA advertises services of its subject: only a trusted TSA leaf qualifies. */
static bool sia_select(const sign_trust* trust, char** url) {
	STACK_OF(X509)* certs = sk_X509_new_null();
	bool ok = certs && certs_parse(&trust->tsa_intermediates, certs) &&
	          certs_parse(&trust->tsa_ca, certs);
	for (int i = 0; ok && i < sk_X509_num(certs); ++i) {
		X509* cert = sk_X509_value(certs, i);
		if (X509_check_ca(cert)) {
			continue;
		}
		AUTHORITY_INFO_ACCESS*
		    sia = X509_get_ext_d2i(cert, NID_sinfo_access, NULL, NULL);
		if (!sia) {
			continue;
		}
		STACK_OF(X509)* chain = chain_verify(cert, trust, true);
		for (int j = 0; chain && ok && j < sk_ACCESS_DESCRIPTION_num(sia);
		     ++j) {
			ACCESS_DESCRIPTION* ad = sk_ACCESS_DESCRIPTION_value(sia, j);
			if (OBJ_obj2nid(ad->method) != NID_ad_timeStamping ||
			    ad->location->type != GEN_URI) {
				continue;
			}
			ASN1_IA5STRING* uri = ad->location->d.uniformResourceIdentifier;
			int             len = ASN1_STRING_length(uri);
			const unsigned char* data = ASN1_STRING_get0_data(uri);
			if (!url_valid(data, len)) {
				continue;
			}
			char* candidate = malloc((size_t)len + 1);
			if (candidate) {
				memcpy(candidate, data, (size_t)len);
				candidate[len] = 0;
			}
			ok = candidate && url_select(url, candidate);
			free(candidate);
		}
		sk_X509_pop_free(chain, X509_free);
		AUTHORITY_INFO_ACCESS_free(sia);
	}
	sk_X509_pop_free(certs, X509_free);
	return ok;
}

static bool catalogue_select(
    const sign_args* args,
    STACK_OF(X509) * chain,
    char** url
) {
	if (args->no_bundled_certs) {
		return true;
	}
	for (int i = 1; i < sk_X509_num(chain); ++i) {
		libssig_trust_anchor anchor = {0};
		unsigned int         len = 0;
		if (!X509_digest(
		        sk_X509_value(chain, i),
		        EVP_sha256(),
		        anchor.sha256,
		        &len
		    ) ||
		    len != 32) {
			return false;
		}
		char hex[65];
		certs_anchor_hex(&anchor, hex);
		for (size_t j = 0; j < sign_certs_count; ++j) {
			const sign_cert* cert = &sign_certs[j];
			if (cert->tsa_url && (cert->usage & SIGN_CERT_DOCUMENT) &&
			    certs_bundle_selected(args, cert->bundle) &&
			    !strcmp(hex, cert->sha256) && !url_select(url, cert->tsa_url)) {
				return false;
			}
		}
	}
	return true;
}

bool timestamp_authority(
    const sign_args*           args,
    const sign_trust*          trust,
    const libssig_certificate* cert,
    char**                     url
) {
	*url = NULL;
	if (args->authority) {
		return url_select(url, args->authority);
	}
	if (args->level == LIBSSIG_LEVEL_B_B) {
		return true;
	}
	const unsigned char* cursor = cert->der;
	X509* signer = cert->der && cert->der_len && cert->der_len <= 65536
	                   ? d2i_X509(NULL, &cursor, (long)cert->der_len)
	                   : NULL;
	STACK_OF(X509)* chain = signer && cursor == cert->der + cert->der_len
	                            ? chain_verify(signer, trust, false)
	                            : NULL;
	bool ok = chain && sia_select(trust, url);
	if (ok && !*url) {
		ok = catalogue_select(args, chain, url);
	}
	ok = ok && *url;
	if (!ok) {
		free(*url);
		*url = NULL;
		fputs(
		    "sign: cannot select a unique TSA from a trusted chain; specify -A "
		    "URL (or -l B-B for offline signing)\n",
		    stderr
		);
	}
	sk_X509_pop_free(chain, X509_free);
	X509_free(signer);
	return ok;
}
