/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_CONFIG_H
#define SIGN_CONFIG_H

#include <sign/arg.h>

/* Loads global settings plus an optional profile. A NULL label loads globals
 * only; a missing global file is allowed. Unchanged on failure. */
bool config_load(
    sign_args*  args,
    const char* label,
    char*       error,
    size_t      error_size
);
/* Shared location and complete schema validation for configuration editors. */
char* config_path(void);
bool  config_validate_source(
     const char* text,
     size_t      len,
     const char* directory,
     char*       error,
     size_t      error_size
 );
void config_free(struct sign_config* config);

#endif
