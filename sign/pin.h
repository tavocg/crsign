/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_PIN_H
#define SIGN_PIN_H
#include <sign/io.h>

#define SIGN_PIN_LIMIT 1024

/* An optional command supplies one UTF-8 line on stdout. Without a command,
 * prompt interactively. Initialize the buffer to zero; release with pin_free.
 */
bool pin_read(const char* const* command, sign_buffer* pin);
void pin_free(sign_buffer* pin);

#endif
