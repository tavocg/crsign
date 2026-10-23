/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <jansson.h>
#include <sign/flow.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	const sign_flow* flow;
	const char*      mode;
	unsigned int     requests;
	GMutex           mutex;
	GCond            condition;
	bool             entered, released;
	bool             ok;
	sign_flow_result result;
	GError*          error;
} run_state;

static bool fake_pin(
    const libssig_token* token,
    sign_buffer*         pin,
    void*                data,
    GError**             error
) {
	(void)token;
	(void)error;
	run_state* state = data;
	++state->requests;
	if (!strcmp(state->mode, "reentrant")) {
		GError*          nested = NULL;
		sign_flow_result result;
		g_assert_false(sign_flow_run(state->flow, NULL, &result, &nested));
		g_assert_error(nested, SIGN_FLOW_ERROR, SIGN_FLOW_ERROR_BUSY);
		g_assert_cmpuint(result.completed, ==, 0);
		g_assert_cmpuint(result.failed_document, ==, SIGN_FLOW_NO_DOCUMENT);
		g_clear_error(&nested);
		sign_flow_result_clear(&result);
	}
	if (!strcmp(state->mode, "concurrent")) {
		g_mutex_lock(&state->mutex);
		state->entered = true;
		g_cond_broadcast(&state->condition);
		while (!state->released) {
			g_cond_wait(&state->condition, &state->mutex);
		}
		g_mutex_unlock(&state->mutex);
	}
	pin->data = malloc(5);
	memcpy(pin->data, "1234", 5);
	pin->len = 4;
	return true;
}
static gpointer run_thread(gpointer data) {
	run_state*     state = data;
	sign_execution execution = {.request_pin = fake_pin, .user_data = state};
	state->ok = sign_flow_run(
	    state->flow,
	    &execution,
	    &state->result,
	    &state->error
	);
	return NULL;
}
int main(int argc, char** argv) {
	if (argc != 4) {
		return 2;
	}
	sign_buffer json = {0};
	char        detail[512];
	if (!file_load(argv[2], &json, detail, sizeof(detail))) {
		return 2;
	}
	GError*    error = NULL;
	sign_flow* flow = sign_flow_parse_json(
	    (char*)json.data,
	    json.len,
	    argv[3],
	    &error
	);
	free(json.data);
	if (!flow) {
		fprintf(stderr, "%s\n", error->message);
		g_clear_error(&error);
		return 1;
	}
	if (!strcmp(argv[1], "parse")) {
		char* serialized = sign_flow_to_json(flow, &error);
		g_assert_nonnull(serialized);
		puts(serialized);
		free(serialized);
		sign_flow_free(flow);
		return 0;
	}
	run_state state = {.flow = flow, .mode = argv[1]};
	if (!strcmp(state.mode, "concurrent")) {
		g_mutex_init(&state.mutex);
		g_cond_init(&state.condition);
		g_mutex_lock(&state.mutex);
		GThread* thread = g_thread_new("flow", run_thread, &state);
		gint64   deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
		while (!state.entered) {
			g_assert_true(
			    g_cond_wait_until(&state.condition, &state.mutex, deadline)
			);
		}
		GError*          busy = NULL;
		sign_flow_result result;
		/* A second parsed flow is also excluded by the process-wide gate. */
		char*      text = sign_flow_to_json(flow, NULL);
		sign_flow* other = sign_flow_parse_json(text, strlen(text), NULL, NULL);
		free(text);
		g_assert_nonnull(other);
		g_assert_false(sign_flow_run(other, NULL, &result, &busy));
		g_assert_error(busy, SIGN_FLOW_ERROR, SIGN_FLOW_ERROR_BUSY);
		g_clear_error(&busy);
		sign_flow_result_clear(&result);
		sign_flow_free(other);
		state.released = true;
		g_cond_broadcast(&state.condition);
		g_mutex_unlock(&state.mutex);
		g_thread_join(thread);
		g_cond_clear(&state.condition);
		g_mutex_clear(&state.mutex);
	} else if (!strcmp(state.mode, "no-pin")) {
		state.ok = sign_flow_run(flow, NULL, &state.result, &state.error);
	} else {
		run_thread(&state);
	}
	if (state.error) {
		fprintf(stderr, "%s\n", state.error->message);
	}
	if (!state.ok) {
		GError*          retry_error = NULL;
		sign_flow_result retry;
		g_assert_false(sign_flow_run(flow, NULL, &retry, &retry_error));
		g_assert_false(
		    g_error_matches(retry_error, SIGN_FLOW_ERROR, SIGN_FLOW_ERROR_BUSY)
		);
		g_clear_error(&retry_error);
		sign_flow_result_clear(&retry);
	}
	json_t* report = json_pack(
	    "{s:b,s:i,s:i,s:i}",
	    "ok",
	    state.ok,
	    "completed",
	    (int)state.result.completed,
	    "failed_document",
	    state.result.failed_document == SIGN_FLOW_NO_DOCUMENT
	        ? -1
	        : (int)state.result.failed_document,
	    "pin_requests",
	    (int)state.requests
	);
	json_t* documents = json_array();
	for (size_t i = 0; i < state.result.count; ++i) {
		const sign_flow_item*            item = sign_flow_get_item(flow, i);
		const sign_flow_document_result* r = &state.result.documents[i];
		g_assert_cmpint(item->operation, ==, r->operation);
		g_assert_true(sign_flow_document(flow, i) == &item->options);
		json_t* document = json_pack(
		    "{s:s,s:b,s:b,s:i,s:b,s:b,s:b,s:i}",
		    "operation",
		    r->operation == SIGN_FLOW_SIGN ? "sign" : "verify",
		    "attempted",
		    r->attempted,
		    "ok",
		    r->ok,
		    "status",
		    (int)r->verification_status,
		    "signature_valid",
		    r->verification.signature_valid,
		    "trust_checked",
		    r->verification.trust_checked,
		    "trusted",
		    r->verification.trusted,
		    "signers",
		    (int)r->verification.count
		);
		json_array_append_new(documents, document);
	}
	json_object_set_new(report, "documents", documents);
	char* output = json_dumps(report, 0);
	puts(output);
	free(output);
	json_decref(report);
	g_clear_error(&state.error);
	sign_flow_result_clear(&state.result);
	sign_flow_result_clear(&state.result);
	sign_flow_free(flow);
	return state.ok ? 0 : 1;
}
