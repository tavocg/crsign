/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/gui/device_access.h>
#include <stdbool.h>

typedef struct {
	GMutex lock;
	GCond  condition;
	bool   entered;
} probe;

static gpointer reader_thread(gpointer data) {
	probe* p = data;
	sign_device_access_reader_lock();
	g_mutex_lock(&p->lock);
	p->entered = true;
	g_cond_broadcast(&p->condition);
	g_mutex_unlock(&p->lock);
	g_usleep(10 * 1000);
	sign_device_access_reader_unlock();
	return NULL;
}
static gpointer writer_thread(gpointer data) {
	probe* p = data;
	sign_device_access_writer_lock();
	g_mutex_lock(&p->lock);
	p->entered = true;
	g_cond_broadcast(&p->condition);
	g_mutex_unlock(&p->lock);
	g_usleep(10 * 1000);
	sign_device_access_writer_unlock();
	return NULL;
}
static void probe_init(probe* p) {
	g_mutex_init(&p->lock);
	g_cond_init(&p->condition);
	p->entered = false;
}
static void probe_clear(probe* p) {
	g_mutex_clear(&p->lock);
	g_cond_clear(&p->condition);
}
static bool wait_entered(probe* p, gint64 timeout) {
	gint64 deadline = g_get_monotonic_time() + timeout;
	g_mutex_lock(&p->lock);
	while (!p->entered && g_get_monotonic_time() < deadline) {
		g_cond_wait_until(&p->condition, &p->lock, deadline);
	}
	bool entered = p->entered;
	g_mutex_unlock(&p->lock);
	return entered;
}
int main(void) {
	probe    p;
	GThread* thread;

	/* An exclusive holder blocks monitoring. */
	probe_init(&p);
	sign_device_access_writer_lock();
	thread = g_thread_new("reader", reader_thread, &p);
	g_usleep(200 * 1000);
	g_assert_false(wait_entered(&p, 0));
	sign_device_access_writer_unlock();
	g_assert_true(wait_entered(&p, 2 * G_TIME_SPAN_SECOND));
	g_thread_join(thread);
	probe_clear(&p);

	/* Monitoring blocks signing until its poll or scan finishes. */
	probe_init(&p);
	sign_device_access_reader_lock();
	thread = g_thread_new("writer", writer_thread, &p);
	g_usleep(200 * 1000);
	g_assert_false(wait_entered(&p, 0));
	sign_device_access_reader_unlock();
	g_assert_true(wait_entered(&p, 2 * G_TIME_SPAN_SECOND));
	g_thread_join(thread);
	probe_clear(&p);

	/* Multiple monitors may share the lock. */
	probe_init(&p);
	sign_device_access_reader_lock();
	thread = g_thread_new("reader", reader_thread, &p);
	g_assert_true(wait_entered(&p, 2 * G_TIME_SPAN_SECOND));
	g_thread_join(thread);
	sign_device_access_reader_unlock();
	probe_clear(&p);
	return 0;
}
