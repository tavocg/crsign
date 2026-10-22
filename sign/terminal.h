/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_TERMINAL_H
#define SIGN_TERMINAL_H

#include <sign/io.h>

/* Interactive CLI only; initialize the buffer to zero. Never read a PIN from
 * redirected input. On success the caller owns a NUL-terminated buffer;
 * release it with terminal_pin_free().
 * PINs larger than 1024 UTF-8 bytes are rejected instead of being truncated. */
bool terminal_pin_read(sign_buffer* pin);
void terminal_pin_free(sign_buffer* pin);

#endif
