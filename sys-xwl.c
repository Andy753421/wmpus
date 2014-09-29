/*
 * Copyright (c) 2014, Andy Spencer <andy753421@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <xkbcommon/xkbcommon.h>
#include <wayland-server.h>
#include <wayland-client.h>

#include <gtk/gtk.h>

#include "util.h"
#include "conf.h"
#include "sys.h"
#include "wm.h"

/* Internal structures */
struct win_sys {
	win_t *win;
};

/* Global data */
static struct wl_display    *display;
static struct wl_event_loop *events;

static GtkWidget *window;

/*****************
 * Gtk Callbacks *
 *****************/

static void on_destroy(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	printf("on_destroy\n");
	sys_exit();
}

static gboolean on_key(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	printf(g_ascii_isprint(event->keyval)
		? "on_key: '%c'\n"
		: "on_key: 0x%X\n",
		event->keyval);
	if (event->keyval == GDK_KEY_q)
		sys_exit();
	if (event->keyval == GDK_KEY_t)
		g_spawn_command_line_async("st-wl", NULL);
	return TRUE;
}

static gboolean on_button(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	printf("on_button\n");
	return TRUE;
}

static gboolean on_move(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
	//printf("on_motion\n");
	return TRUE;
}

static gboolean on_wayland(gpointer user_data)
{
	// TODO - convert to polled execution
	wl_display_flush_clients(display);
	wl_event_loop_dispatch(events, 0);
	return TRUE;
}

/*********************
 * Wayland Callbacks *
 *********************/

#if 0
static void new_window(void)
{
	printf("new_window\n");
}

static void new_screen(void)
{
	printf("new_screen\n");
}
#endif

/***********************************
 * Wayland Shared Memory Interface *
 ***********************************/

struct wl_global *shm_ref;

static struct wl_shm_interface shm_iface = {
	.create_pool = NULL,
};

static void shm_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	printf("shm_bind\n");

	struct wl_resource *res = wl_resource_create(client, &wl_shm_interface, version, id);
	wl_resource_set_implementation(res, &shm_iface, NULL, NULL);

	wl_shm_send_format(res, WL_SHM_FORMAT_XRGB8888);
	wl_shm_send_format(res, WL_SHM_FORMAT_ARGB8888);
}

/********************
 * System functions *
 ********************/
void sys_move(win_t *win, int x, int y, int w, int h)
{
	printf("sys_move: %p - %d,%d  %dx%d\n",
			win, x, y, w, h);
}

void sys_raise(win_t *win)
{
	printf("sys_raise: %p\n", win);
}

void sys_focus(win_t *win)
{
	printf("sys_focus: %p\n", win);
}

void sys_show(win_t *win, state_t state)
{
	printf("sys_show: %p: %d", win, state);
}

void sys_watch(win_t *win, event_t ev, mod_t mod)
{
	printf("sys_watch: %p - %x %hhx\n",
			win, ev, mod2int(mod));
}

void sys_unwatch(win_t *win, event_t ev, mod_t mod)
{
	printf("sys_unwatch: %p - %x %hhx\n",
			win, ev, mod2int(mod));
}

list_t *sys_info(win_t *win)
{
	printf("sys_info: %p\n", win);
	return list_insert(NULL, win);
}

win_t *sys_init(void)
{
	printf("sys_init\n");

	/* Register log handler */
	wl_log_set_handler_server((wl_log_func_t)vprintf);

	/* Open the display */
	if (!(display = wl_display_create()))
		error("Unable to  create display");
	if (wl_display_add_socket(display, NULL) != 0)
		error("Unable to add socket");
	if (!(events = wl_display_get_event_loop(display)))
		error("Unable to get event loop");

	/* Register interfaces */
	shm_ref = wl_global_create(display, &wl_shm_interface, 1, NULL, &shm_bind);

	/* Setup GTK display */
	gtk_init(&conf_argc, &conf_argv);
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_widget_add_events(window,
			GDK_KEY_PRESS_MASK |
			GDK_BUTTON_PRESS_MASK |
			GDK_BUTTON_RELEASE_MASK |
			GDK_POINTER_MOTION_MASK);
	g_signal_connect(window, "destroy",             G_CALLBACK(on_destroy), NULL);
	g_signal_connect(window, "key-press-event",     G_CALLBACK(on_key),     NULL);
	g_signal_connect(window, "button-press-event",  G_CALLBACK(on_button),  NULL);
	g_signal_connect(window, "motion-notify-event", G_CALLBACK(on_move),    NULL);
	g_timeout_add(1000/60, on_wayland, NULL);
	gtk_widget_show(window);

	return new0(win_t);
}

void sys_run(win_t *root)
{
	printf("sys_run: %p\n", root);
	gtk_main();
}

void sys_exit(void)
{
	printf("sys_exit\n");
	gtk_main_quit();
}

void sys_free(win_t *root)
{
	printf("sys_free: %p\n", root);
}
