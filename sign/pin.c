/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <gio/gio.h>
#include <sign/pin.h>
#include <sign/terminal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>

typedef struct {
	HANDLE process;
	HANDLE output;
} pin_child;

/* Quote every argument using the Windows C runtime rules, including empty
 * strings. GLib's Windows spawn quoting can drop empty arguments. */
static gunichar2* command_line(const char* const* command) {
	GString* line = g_string_new(NULL);
	for (size_t i = 0; command[i]; ++i) {
		if (i) {
			g_string_append_c(line, ' ');
		}
		g_string_append_c(line, '"');
		size_t slashes = 0;
		for (const char* p = command[i];; ++p) {
			if (*p == '\\') {
				++slashes;
				continue;
			}
			size_t count = (*p == '"' || !*p) ? slashes * 2 : slashes;
			for (size_t j = 0; j < count; ++j) {
				g_string_append_c(line, '\\');
			}
			slashes = 0;
			if (!*p) {
				break;
			}
			if (*p == '"') {
				g_string_append_c(line, '\\');
			}
			g_string_append_c(line, *p);
		}
		g_string_append_c(line, '"');
	}
	gunichar2* wide = g_utf8_to_utf16(line->str, -1, NULL, NULL, NULL);
	g_string_free(line, true);
	return wide;
}

static bool child_start(const char* const* command, pin_child* child) {
	char* executable = g_find_program_in_path(command[0]);
	gunichar2* application = executable
	                            ? g_utf8_to_utf16(executable, -1, NULL, NULL, NULL)
	                            : NULL;
	gunichar2* line = command_line(command);
	g_free(executable);
	SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
	HANDLE output = NULL, writer = NULL;
	HANDLE input = INVALID_HANDLE_VALUE, errors = INVALID_HANDLE_VALUE;
	LPPROC_THREAD_ATTRIBUTE_LIST attributes = NULL;
	bool ok = false;
	if (!application || !line || !CreatePipe(&output, &writer, &security, 0) ||
	    !SetHandleInformation(output, HANDLE_FLAG_INHERIT, 0)) {
		goto done;
	}
	input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
	                    &security, OPEN_EXISTING, 0, NULL);
	errors = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
	                     &security, OPEN_EXISTING, 0, NULL);
	if (input == INVALID_HANDLE_VALUE || errors == INVALID_HANDLE_VALUE) {
		goto done;
	}
	SIZE_T size = 0;
	InitializeProcThreadAttributeList(NULL, 1, 0, &size);
	attributes = g_malloc(size);
	if (!InitializeProcThreadAttributeList(attributes, 1, 0, &size)) {
		g_free(attributes);
		attributes = NULL;
		goto done;
	}
	HANDLE handles[] = {input, writer, errors};
	if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
	                               handles, sizeof(handles), NULL, NULL)) {
		goto done;
	}
	STARTUPINFOEXW startup = {0};
	startup.StartupInfo.cb = sizeof(startup);
	startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	startup.StartupInfo.hStdInput = input;
	startup.StartupInfo.hStdOutput = writer;
	startup.StartupInfo.hStdError = errors;
	startup.lpAttributeList = attributes;
	PROCESS_INFORMATION process = {0};
	if (CreateProcessW((WCHAR*)application, (WCHAR*)line, NULL, NULL, TRUE,
	                   EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW,
	                   NULL, NULL, &startup.StartupInfo, &process)) {
		CloseHandle(process.hThread);
		*child = (pin_child){process.hProcess, output};
		output = NULL;
		ok = true;
	}
done:
	if (attributes) {
		DeleteProcThreadAttributeList(attributes);
		g_free(attributes);
	}
	if (output) CloseHandle(output);
	if (writer) CloseHandle(writer);
	if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
	if (errors != INVALID_HANDLE_VALUE) CloseHandle(errors);
	g_free(application);
	g_free(line);
	return ok;
}
static gssize child_read(pin_child* child, void* data, size_t capacity) {
	DWORD count = 0;
	if (ReadFile(child->output, data, (DWORD)capacity, &count, NULL)) {
		return (gssize)count;
	}
	return GetLastError() == ERROR_BROKEN_PIPE ? 0 : -1;
}
static void child_stop(pin_child* child) {
	TerminateProcess(child->process, 1);
}
static bool child_finish(pin_child* child) {
	DWORD status = 1;
	bool ok = WaitForSingleObject(child->process, INFINITE) == WAIT_OBJECT_0 &&
	          GetExitCodeProcess(child->process, &status) && status == 0;
	CloseHandle(child->output);
	CloseHandle(child->process);
	return ok;
}
#else
typedef GSubprocess* pin_child;
static bool child_start(const char* const* command, pin_child* child) {
	*child = g_subprocess_newv(command, G_SUBPROCESS_FLAGS_STDOUT_PIPE |
	                                   G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL);
	return *child != NULL;
}
static gssize child_read(pin_child* child, void* data, size_t capacity) {
	return g_input_stream_read(g_subprocess_get_stdout_pipe(*child), data,
	                           capacity, NULL, NULL);
}
static void child_stop(pin_child* child) {
	g_subprocess_force_exit(*child);
}
static bool child_finish(pin_child* child) {
	bool ok = g_subprocess_wait_check(*child, NULL, NULL);
	g_object_unref(*child);
	return ok;
}
#endif

void pin_free(sign_buffer* pin) {
	if (pin->data) {
		volatile unsigned char* cursor = pin->data;
		for (size_t i = 0; i <= pin->len; ++i) {
			cursor[i] = 0;
		}
		free(pin->data);
	}
	*pin = (sign_buffer){0};
}

bool pin_read(const char* const* command, sign_buffer* pin) {
	if (!command) {
		return terminal_pin_read(pin);
	}
	/* stdin defaults to the null device. Do not expose helper stderr or use a
	 * shell implicitly; either could leak secrets into diagnostics/JSON. */
	pin_child child;
	if (!child_start(command, &child)) {
		fputs("sign: cannot start pincmd\n", stderr);
		return false;
	}
	/* Room for a maximum-length PIN, CRLF, and one byte to detect overflow.
	 * Mark the entire allocation for wiping, even after an unsuccessful read.
	 */
	const size_t  capacity = SIGN_PIN_LIMIT + 3;
	sign_buffer   output = {.data = malloc(capacity), .len = capacity - 1};
	bool          ok = output.data != NULL;
	size_t        used = 0;
	while (ok && used < capacity) {
		gssize count = child_read(
		    &child,
		    output.data + used,
		    capacity - used
		);
		if (count < 0) {
			ok = false;
		} else if (!count) {
			break;
		} else {
			used += (size_t)count;
		}
	}
	if (used == capacity) {
		ok = false;
	}
	if (!ok) {
		child_stop(&child);
	}
	if (!child_finish(&child)) {
		ok = false;
	}
	if (ok) {
		if (used && output.data[used - 1] == '\n') {
			--used;
			if (used && output.data[used - 1] == '\r') {
				--used;
			}
		}
		ok = used > 0 && used <= SIGN_PIN_LIMIT &&
		     !memchr(output.data, 0, used) &&
		     !memchr(output.data, '\n', used) &&
		     !memchr(output.data, '\r', used) &&
		     g_utf8_validate((const char*)output.data, (gssize)used, NULL);
	}
	if (ok) {
		pin->data = malloc(used + 1);
		ok = pin->data != NULL;
		if (ok) {
			memcpy(pin->data, output.data, used);
			pin->data[used] = 0;
			pin->len = used;
		}
	}
	pin_free(&output);
	if (!ok) {
		fputs("sign: pincmd failed or returned an invalid PIN\n", stderr);
	}
	return ok;
}
