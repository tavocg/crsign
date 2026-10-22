/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_CONFIG_H
#define SIGN_CONFIG_H

#include <sign/arg.h>

/* Replaces options with profile values and defaults. Unchanged on failure. */
bool config_profile(
    sign_args*  args,
    const char* label,
    char*       error,
    size_t      error_size
);
void config_free(struct sign_config* config);

#endif
