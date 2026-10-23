/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_FLOW_H
#define SIGN_FLOW_H
#include <sign/execution.h>

#define SIGN_FLOW_VERSION 1
#define SIGN_FLOW_MAX_DOCUMENTS 1024
#define SIGN_FLOW_MAX_JSON_SIZE (8u * 1024u * 1024u)
#define SIGN_FLOW_NO_DOCUMENT ((size_t)-1)

typedef struct sign_flow sign_flow;
typedef enum {
	SIGN_FLOW_ERROR_INVALID,
	SIGN_FLOW_ERROR_VERSION,
	SIGN_FLOW_ERROR_BUSY
} sign_flow_error;
#define SIGN_FLOW_ERROR (sign_flow_error_quark())
GQuark sign_flow_error_quark(void);

typedef enum { SIGN_FLOW_SIGN, SIGN_FLOW_VERIFY } sign_flow_operation;
typedef struct {
	sign_flow_operation operation;
	sign_options        options;
	const char* content; /* Original content for detached verification. */
} sign_flow_item;
typedef struct {
	sign_flow_operation   operation;
	bool                  attempted, ok;
	sign_document_result  signing;
	libssig_status        verification_status;
	libssig_verify_result verification;
} sign_flow_document_result;
typedef struct {
	size_t completed;
	/* Zero-based index of the failed document, or SIGN_FLOW_NO_DOCUMENT. */
	size_t                     failed_document;
	sign_document_result       document;
	size_t                     count;
	sign_flow_document_result* documents;
} sign_flow_result;

/* Parse/validate the complete flow without reading documents or accessing
 * tokens. All relative filesystem paths resolve against base_directory (NULL
 * snapshots the current directory). No TOML/configuration, commands or PINs are
 * loaded. The returned immutable flow owns all its settings. */
sign_flow* sign_flow_parse_json(
    const char* json,
    size_t      len,
    const char* base_directory,
    GError**    error
);
/* Release owned per-document reports before reusing a result. Zero initialized
 * and cleared results may also be passed here. */
void                  sign_flow_result_clear(sign_flow_result* result);
const sign_flow_item* sign_flow_get_item(const sign_flow* flow, size_t index);
size_t                sign_flow_count(const sign_flow* flow);
const sign_options*   sign_flow_document(const sign_flow* flow, size_t index);
/* Canonical JSON with absolute paths and resolved output paths. Free with
 * free(). */
char* sign_flow_to_json(const sign_flow* flow, GError** error);
void  sign_flow_free(sign_flow* flow);

/* Synchronous, fail-fast execution. At most one flow runs per process, even
 * across threads or reentrant calls. Keep flow/execution alive for the call.
 * Earlier outputs remain after a failure; no later document is started.
 * No terminal PIN prompt: provide an execution-time callback when required. */
bool sign_flow_run(
    const sign_flow*      flow,
    const sign_execution* execution,
    sign_flow_result*     result,
    GError**              error
);
#endif
