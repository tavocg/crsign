/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <sign/terminal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <signal.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

typedef enum {
	PIN_OK,
	PIN_NOT_TERMINAL,
	PIN_CANCELLED,
	PIN_TOO_LONG,
	PIN_ERROR
} pin_status;

static void wipe(void* data, size_t len) {
	volatile unsigned char* cursor = data;
	while (len--) {
		*cursor++ = 0;
	}
}

static bool prompt(void) {
	return fputs("PIN: ", stderr) >= 0 && fflush(stderr) == 0;
}

static bool mask_add(void) {
	return fputc('*', stderr) != EOF && fflush(stderr) == 0;
}

static bool mask_erase(size_t count) {
	while (count--) {
		if (fputs("\b \b", stderr) < 0) {
			return false;
		}
	}
	return fflush(stderr) == 0;
}

#ifdef _WIN32
/* The CLI asks for one PIN at a time. Restore console mode for Windows control
 * events delivered while PIN entry is active. */
static HANDLE        pin_console;
static DWORD         pin_console_mode;
static volatile LONG pin_active;

static BOOL WINAPI console_control(DWORD event) {
	(void)event;
	if (InterlockedCompareExchange(&pin_active, 0, 0)) {
		SetConsoleMode(pin_console, pin_console_mode);
	}
	return FALSE; /* Continue with the process's normal control handlers. */
}

static pin_status read_pin(unsigned char* data, size_t* len) {
	HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
	DWORD  mode;
	if (!GetConsoleMode(input, &mode)) {
		return PIN_NOT_TERMINAL;
	}
	pin_console = input;
	pin_console_mode = mode;
	if (!SetConsoleCtrlHandler(console_control, TRUE)) {
		return PIN_ERROR;
	}
	InterlockedExchange(&pin_active, 1);
	DWORD hidden = mode &
	               ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT |
	                 ENABLE_PROCESSED_INPUT | ENABLE_VIRTUAL_TERMINAL_INPUT);
	pin_status status = PIN_ERROR;
	WCHAR      wide[SIGN_PIN_LIMIT + 1] = {0};
	size_t     count = 0;
	bool       overflow = false;
	if (!SetConsoleMode(input, hidden) || !prompt()) {
		goto done;
	}
	for (;;) {
		WCHAR c = 0;
		DWORD read = 0;
		if (!ReadConsoleW(input, &c, 1, &read, NULL) || read != 1) {
			goto done;
		}
		if (c == 3 || c == 4 || c == 26) {
			status = PIN_CANCELLED;
			goto done;
		}
		if (c == '\r' || c == '\n') {
			break;
		}
		if (c == '\b') {
			if (count && !overflow) {
				--count;
				if (count && wide[count] >= 0xdc00 && wide[count] <= 0xdfff &&
				    wide[count - 1] >= 0xd800 && wide[count - 1] <= 0xdbff) {
					wide[count--] = 0;
				}
				wide[count] = 0;
				if (!mask_erase(1)) {
					goto done;
				}
			}
		} else if (c == 21) {
			size_t stars = 0;
			for (size_t i = 0; i < count; ++i) {
				if (wide[i] < 0xdc00 || wide[i] > 0xdfff) {
					++stars;
				}
			}
			wipe(wide, sizeof(wide));
			count = 0;
			overflow = false;
			if (!mask_erase(stars)) {
				goto done;
			}
		} else if (!c) {
			goto done;
		} else if (count < SIGN_PIN_LIMIT && !overflow) {
			wide[count++] = c;
			if ((c < 0xdc00 || c > 0xdfff) && !mask_add()) {
				goto done;
			}
		} else {
			overflow = true;
		}
	}
	if (overflow) {
		status = PIN_TOO_LONG;
	} else if (!count) {
		*len = 0;
		status = PIN_OK;
	} else {
		int bytes = WideCharToMultiByte(
		    CP_UTF8,
		    WC_ERR_INVALID_CHARS,
		    wide,
		    (int)count,
		    (char*)data,
		    SIGN_PIN_LIMIT,
		    NULL,
		    NULL
		);
		if (bytes) {
			*len = (size_t)bytes;
			status = PIN_OK;
		} else if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
			status = PIN_TOO_LONG;
		}
	}
done:
	wipe(wide, sizeof(wide));
	if (status != PIN_OK) {
		FlushConsoleInputBuffer(input);
	}
	if (!SetConsoleMode(input, mode)) {
		status = PIN_ERROR;
	}
	InterlockedExchange(&pin_active, 0);
	if (!SetConsoleCtrlHandler(console_control, FALSE)) {
		status = PIN_ERROR;
	}
	fputc('\n', stderr);
	return status;
}
#else
static volatile sig_atomic_t pin_signal;

static void interrupted(int signal_number) {
	pin_signal = signal_number;
}

static pin_status read_pin(unsigned char* data, size_t* len) {
	if (!isatty(STDIN_FILENO)) {
		return PIN_NOT_TERMINAL;
	}
	struct termios original;
	if (tcgetattr(STDIN_FILENO, &original) < 0) {
		return PIN_ERROR;
	}
	const int signals[] =
	    {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU};
	struct sigaction previous[sizeof(signals) / sizeof(signals[0])];
	struct sigaction handler = {0};
	handler.sa_handler = interrupted;
	sigemptyset(&handler.sa_mask);
	sigset_t blocked, old_mask;
	sigemptyset(&blocked);
	for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i) {
		sigaddset(&blocked, signals[i]);
	}
	if (sigprocmask(SIG_BLOCK, &blocked, &old_mask) < 0) {
		return PIN_ERROR;
	}
	pin_signal = 0;
	size_t     installed = 0;
	bool       changed = false;
	pin_status status = PIN_ERROR;
	for (; installed < sizeof(signals) / sizeof(signals[0]); ++installed) {
		if (sigaction(signals[installed], &handler, &previous[installed]) < 0) {
			goto done;
		}
	}
	struct termios hidden = original;
	/* Read bytes directly so a platform's canonical line limit cannot silently
	 * truncate a PIN before the application checks its length. */
	hidden.c_lflag &= (tcflag_t) ~(ECHO | ECHONL | ICANON);
	hidden.c_cc[VMIN] = 1;
	hidden.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden) < 0) {
		goto done;
	}
	changed = true;
	if (!prompt()) {
		goto done;
	}
	bool overflow = false;
	for (;;) {
		if (pin_signal) {
			status = PIN_CANCELLED;
			break;
		}
		fd_set ready;
		FD_ZERO(&ready);
		FD_SET(STDIN_FILENO, &ready);
		/* Unblock signals atomically while waiting; cleanup runs with them
		 * blocked, so no interrupt can leave echo disabled during restoration.
		 */
		int available = pselect(
		    STDIN_FILENO + 1,
		    &ready,
		    NULL,
		    NULL,
		    NULL,
		    &old_mask
		);
		if (available < 0) {
			if (errno == EINTR) {
				continue;
			}
			break;
		}
		unsigned char c = 0;
		ssize_t       count = read(STDIN_FILENO, &c, 1);
		if (count != 1) {
			status = count == 0 ? PIN_CANCELLED : PIN_ERROR;
			break;
		}
		if (c == '\n' || c == '\r') {
			status = overflow ? PIN_TOO_LONG : PIN_OK;
			break;
		}
		if (c == original.c_cc[VEOF]) {
			status = PIN_CANCELLED;
			break;
		}
		if (c == original.c_cc[VKILL]) {
			size_t stars = 0;
			for (size_t i = 0; i < *len; ++i) {
				if ((data[i] & 0xc0) != 0x80) {
					++stars;
				}
			}
			if (!mask_erase(stars)) {
				break;
			}
			wipe(data, *len);
			*len = 0;
			overflow = false;
			continue;
		}
		if (c == original.c_cc[VERASE] || c == 8 || c == 127) {
			/* Erase a whole UTF-8 codepoint; never leave half of a PIN
			 * character. */
			bool erase = *len && !overflow;
			while (*len && !overflow) {
				--*len;
				bool continuation = (data[*len] & 0xc0) == 0x80;
				data[*len] = 0;
				if (!continuation) {
					break;
				}
			}
			if (erase && !mask_erase(1)) {
				break;
			}
			continue;
		}
		if (!c) {
			break;
		}
		if (*len < SIGN_PIN_LIMIT) {
			data[(*len)++] = c;
			if ((c & 0xc0) != 0x80 && !mask_add()) {
				break;
			}
		} else {
			overflow = true;
		}
	}
done:
	if (changed) {
		if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &original) < 0) {
			status = PIN_ERROR;
		}
		fputc('\n', stderr);
	}
	while (installed) {
		--installed;
		if (sigaction(signals[installed], &previous[installed], NULL) < 0) {
			status = PIN_ERROR;
		}
	}
	int caught = pin_signal;
	pin_signal = 0;
	if (caught) {
		kill(getpid(), caught);
	}
	if (sigprocmask(SIG_SETMASK, &old_mask, NULL) < 0) {
		status = PIN_ERROR;
	}
	return status;
}
#endif

bool terminal_pin_read(sign_buffer* pin) {
	unsigned char data[SIGN_PIN_LIMIT + 1] = {0};
	size_t        len = 0;
	pin_status    status = read_pin(data, &len);
	if (status == PIN_OK) {
		pin->data = malloc(len + 1);
		if (pin->data) {
			memcpy(pin->data, data, len);
			pin->data[len] = 0;
			pin->len = len;
		} else {
			status = PIN_ERROR;
		}
	}
	wipe(data, sizeof(data));
	if (status == PIN_OK) {
		return true;
	}
	const char* messages[] = {
	    "",
	    "an interactive terminal is required for the PIN",
	    "PIN entry cancelled",
	    "PIN exceeds 1024 bytes",
	    "cannot read PIN"
	};
	fprintf(stderr, "sign: %s\n", messages[status]);
	return false;
}
