/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A loadable PKCS#11 module with one empty hardware token, no real keys/PINs.
 */
#define CRYPTOKI_EXPORTS 1
#include <p11-kit/pkcs11.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void trace(const char* event) {
	const char* path = getenv("SIGN_TEST_MODULE_LOG");
	FILE*       out = path ? fopen(path, "a") : NULL;
	if (out) {
		fprintf(out, "%s:%s\n", MODULE_LABEL, event);
		fclose(out);
	}
}
static CK_RV initialize(CK_VOID_PTR args) {
	(void)args;
	trace("initialize");
#ifdef MODULE_FAIL
	return CKR_GENERAL_ERROR;
#else
	return CKR_OK;
#endif
}
static CK_RV finalize(CK_VOID_PTR args) {
	(void)args;
	trace("finalize");
	return CKR_OK;
}
static void padded(CK_UTF8CHAR* out, size_t size, const char* text) {
	memset(out, ' ', size);
	size_t length = strlen(text);
	memcpy(out, text, length < size ? length : size);
}
static CK_RV info(CK_INFO_PTR out) {
	memset(out, 0, sizeof(*out));
	out->cryptokiVersion = (CK_VERSION){2, 40};
	padded(out->manufacturerID, sizeof(out->manufacturerID), "CRSign tests");
	padded(
	    out->libraryDescription,
	    sizeof(out->libraryDescription),
	    MODULE_LABEL
	);
	return CKR_OK;
}
static CK_RV slots(CK_BBOOL present, CK_SLOT_ID_PTR out, CK_ULONG_PTR count) {
	(void)present;
	if (out) {
		if (!*count) {
			return CKR_BUFFER_TOO_SMALL;
		}
		*out = 1;
	}
	*count = 1;
	return CKR_OK;
}
static CK_RV slot_info(CK_SLOT_ID slot, CK_SLOT_INFO_PTR out) {
	(void)slot;
	memset(out, 0, sizeof(*out));
	out->flags = CKF_HW_SLOT | CKF_TOKEN_PRESENT;
	padded(out->slotDescription, sizeof(out->slotDescription), MODULE_LABEL);
	padded(out->manufacturerID, sizeof(out->manufacturerID), "CRSign tests");
	return CKR_OK;
}
static CK_RV token_info(CK_SLOT_ID slot, CK_TOKEN_INFO_PTR out) {
	(void)slot;
	memset(out, 0, sizeof(*out));
	padded(out->label, sizeof(out->label), MODULE_LABEL);
	padded(out->manufacturerID, sizeof(out->manufacturerID), "CRSign tests");
	padded(out->model, sizeof(out->model), "Fixture");
	padded(out->serialNumber, sizeof(out->serialNumber), MODULE_LABEL);
	out->flags = CKF_TOKEN_INITIALIZED;
	return CKR_OK;
}
static CK_RV open_session(
    CK_SLOT_ID            slot,
    CK_FLAGS              flags,
    CK_VOID_PTR           app,
    CK_NOTIFY             notify,
    CK_SESSION_HANDLE_PTR out
) {
	(void)slot;
	(void)flags;
	(void)app;
	(void)notify;
	*out = 1;
	return CKR_OK;
}
static CK_RV close_session(CK_SESSION_HANDLE session) {
	(void)session;
	return CKR_OK;
}
static CK_RV find_init(
    CK_SESSION_HANDLE session,
    CK_ATTRIBUTE_PTR  attrs,
    CK_ULONG          count
) {
	(void)session;
	(void)attrs;
	(void)count;
	return CKR_OK;
}
static CK_RV find(
    CK_SESSION_HANDLE    session,
    CK_OBJECT_HANDLE_PTR out,
    CK_ULONG             maximum,
    CK_ULONG_PTR         count
) {
	(void)session;
	(void)out;
	(void)maximum;
	*count = 0;
	return CKR_OK;
}
CK_RV                   C_GetFunctionList(CK_FUNCTION_LIST_PTR_PTR out);
static CK_FUNCTION_LIST functions = {
    .version = {2, 40},
    .C_Initialize = initialize,
    .C_Finalize = finalize,
    .C_GetInfo = info,
    .C_GetFunctionList = C_GetFunctionList,
    .C_GetSlotList = slots,
    .C_GetSlotInfo = slot_info,
    .C_GetTokenInfo = token_info,
    .C_OpenSession = open_session,
    .C_CloseSession = close_session,
    .C_FindObjectsInit = find_init,
    .C_FindObjects = find,
    .C_FindObjectsFinal = close_session,
};
CK_RV C_GetFunctionList(CK_FUNCTION_LIST_PTR_PTR out) {
	*out = &functions;
	return CKR_OK;
}
