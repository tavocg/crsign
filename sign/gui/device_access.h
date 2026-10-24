/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_GUI_DEVICE_ACCESS_H
#define SIGN_GUI_DEVICE_ACCESS_H
#include <glib.h>

/* Serialize background device monitoring with operations that open PKCS#11
 * sessions or perform card I/O. Monitoring holds the shared lock for a poll or
 * scan; signing and preview hold the exclusive lock while they use a token.
 * The locks are process-wide and must be paired by the same thread. */
void sign_device_access_reader_lock(void);
void sign_device_access_reader_unlock(void);
void sign_device_access_writer_lock(void);
void sign_device_access_writer_unlock(void);

#endif
