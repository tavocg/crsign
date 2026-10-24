/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/gui/device_access.h>

static GRWLock access;
static gsize   access_ready;

static GRWLock* lock(void) {
	if (g_once_init_enter(&access_ready)) {
		g_rw_lock_init(&access);
		g_once_init_leave(&access_ready, 1);
	}
	return &access;
}
void sign_device_access_reader_lock(void) {
	g_rw_lock_reader_lock(lock());
}
void sign_device_access_reader_unlock(void) {
	g_rw_lock_reader_unlock(lock());
}
void sign_device_access_writer_lock(void) {
	g_rw_lock_writer_lock(lock());
}
void sign_device_access_writer_unlock(void) {
	g_rw_lock_writer_unlock(lock());
}
