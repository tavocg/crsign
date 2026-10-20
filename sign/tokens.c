/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/certificate.h>
#include <sign/json.h>
#include <sign/tokens.h>
#include <stdio.h>
#include <stdlib.h>

static void certificate_json(const libssig_certificate* cert, bool content) {
	fputs("{\"id_hex\":", stdout);
	json_hex(cert->id, cert->id_len);
	printf(",\"id_len\":%zu,\"label\":", cert->id_len);
	json_text((const unsigned char*)cert->label, cert->label_len);
	printf(",\"label_len\":%zu,\"der_hex\":", cert->label_len);
	json_hex(cert->der, cert->der_len);
	printf(",\"der_len\":%zu,\"key_usage\":", cert->der_len);
	if (cert->key_usage_present) {
		printf("%u", cert->key_usage);
	} else {
		fputs("null", stdout);
	}
	fputs(",\"key_usage_names\":", stdout);
	json_strings(&cert->key_usage_names);
	const char* statuses[] = {"unavailable", "valid", "invalid"};
	fputs(",\"metadata_status\":", stdout);
	json_string(statuses[cert->metadata_status]);
	fputs(",\"extended_key_usage\":", stdout);
	if (cert->extended_key_usage_present) {
		json_strings(&cert->extended_key_usage);
	} else {
		fputs("null", stdout);
	}
	fputs(",\"certificate_policies\":", stdout);
	if (cert->certificate_policies_present) {
		json_strings(&cert->certificate_policies);
	} else {
		fputs("null", stdout);
	}
	if (content) {
		fputs(",\"content\":", stdout);
		certificate_content(cert, true);
	}
	putchar('}');
}

static void token_json(
    const libssig_token*            token,
    const libssig_certificate_list* certs,
    bool                            content
) {
	fputs("{\"uri\":", stdout);
	json_string(token->uri);
	fputs(",\"label\":", stdout);
	json_string(token->label);
	fputs(",\"manufacturer\":", stdout);
	json_string(token->manufacturer);
	fputs(",\"model\":", stdout);
	json_string(token->model);
	fputs(",\"serial\":", stdout);
	json_string(token->serial);
	printf(",\"flags\":%lu,\"flag_names\":", token->flags);
	json_strings(&token->flag_names);
	fputs(",\"certificates\":[", stdout);
	for (size_t i = 0; i < certs->count; ++i) {
		if (i) {
			putchar(',');
		}
		certificate_json(&certs->items[i], content);
	}
	fputs("]}", stdout);
}

static void token_text(
    const libssig_token*            token,
    const libssig_certificate_list* certs,
    bool                            content
) {
	printf(
	    "Token: %s\n  URI: %s\n  Manufacturer: %s\n  Serial: %s\n",
	    token->label,
	    token->uri,
	    token->manufacturer,
	    token->serial
	);
	printf("  Certificates: %zu\n", certs->count);
	for (size_t i = 0; i < certs->count; ++i) {
		const libssig_certificate* cert = &certs->items[i];
		fputs("    Label: ", stdout);
		if (cert->label) {
			fwrite(cert->label, 1, cert->label_len, stdout);
		}
		fputs("\n    ID: ", stdout);
		for (size_t j = 0; j < cert->id_len; ++j) {
			printf("%02x", cert->id[j]);
		}
		putchar('\n');
		if (content) {
			certificate_content(cert, false);
		}
	}
}

static void tokens_output(
    const sign_args*                args,
    libssig_status                  status,
    const libssig_token_list*       tokens,
    const libssig_certificate_list* certs
) {
	if (status == LIBSSIG_OK) {
		if (args->json) {
			printf(
			    "{\"command\":\"tokens\",\"ok\":true,\"count\":%zu,\"tokens\":"
			    "[",
			    tokens->count
			);
		} else {
			printf("Tokens: %zu\n", tokens->count);
		}
		for (size_t i = 0; i < tokens->count; ++i) {
			if (args->json) {
				if (i) {
					putchar(',');
				}
				token_json(&tokens->items[i], &certs[i], args->content);
			} else {
				token_text(&tokens->items[i], &certs[i], args->content);
			}
		}
		if (args->json) {
			puts("]}");
		}
	} else {
		fprintf(
		    stderr,
		    "sign: listing tokens failed (status=%d)\n",
		    (int)status
		);
		if (args->json) {
			printf(
			    "{\"command\":\"tokens\",\"ok\":false,\"status\":%d}\n",
			    (int)status
			);
		}
	}
}

int tokens_run(const sign_args* args) {
	libssig_context           ctx = {0};
	libssig_token_list        tokens = {0};
	libssig_certificate_list* certs = NULL;
	libssig_status            status = libssig_context_open(&ctx);
	if (status == LIBSSIG_OK) {
		status = libssig_tokens(&ctx, &tokens, 0);
	}

	if (status == LIBSSIG_OK && tokens.count) {
		certs = calloc(tokens.count, sizeof(*certs));
		if (!certs) {
			status = LIBSSIG_ERROR_MEMORY;
		}
	}
	for (size_t i = 0; status == LIBSSIG_OK && i < tokens.count; ++i) {
		status = libssig_certificates(
		    &ctx,
		    &tokens.items[i],
		    &certs[i],
		    args->all_certificates ? LIBSSIG_CERTIFICATES_ALL : 0
		);
	}
	tokens_output(args, status, &tokens, certs);
	if (certs) {
		for (size_t i = 0; i < tokens.count; ++i) {
			libssig_certificates_free(&certs[i]);
		}
	}
	free(certs);
	libssig_tokens_free(&tokens);
	libssig_context_close(&ctx);
	return status == LIBSSIG_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
