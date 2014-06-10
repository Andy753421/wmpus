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
#include <swc.h>

#include "util.h"
#include "conf.h"
#include "sys.h"
#include "wm.h"

/* Internal structures */
struct win_sys {
	win_t              *win;
	struct swc_window  *swc;
	struct wl_listener  evt;
};

/* Global data */
static struct wl_display    *display;
static struct wl_event_loop *events;

/*******************
 * Debug functions *
 *******************/
static const char * cmd_term[] = { "st-wl", NULL };
static const char * cmd_menu[] = { "dmenu_run-wl", NULL };

static void cmd_exit(void *data, uint32_t time, uint32_t value, uint32_t state)
{
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
		return;

	printf("cmd_exit\n");
	exit(0);
}

static void cmd_spawn(void *data, uint32_t time, uint32_t value, uint32_t state)
{
	char **cmd = data;
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
		return;

	printf("cmd_spawn: %s\n", cmd[0]);
	if (fork() == 0) {
		execvp(cmd[0], cmd);
		exit(0);
	}
}

/*************
 * Callbacks *
 *************/

#define listener2win(listener) \
	(((win_sys_t*)((size_t)listener - \
	       (size_t)(&((win_sys_t*)NULL)->evt)))->win)

static const char *map_window[] = {
	[SWC_WINDOW_DESTROYED]                = "destroyed",
	[SWC_WINDOW_TITLE_CHANGED]            = "title_changed",
	[SWC_WINDOW_CLASS_CHANGED]            = "class_changed",
	[SWC_WINDOW_STATE_CHANGED]            = "state_changed",
	[SWC_WINDOW_ENTERED]                  = "entered",
	[SWC_WINDOW_RESIZED]                  = "resized",
	[SWC_WINDOW_PARENT_CHANGED]           = "parent_changed",
};

static const char *map_screen[] = {
	[SWC_SCREEN_DESTROYED]                = "destroyed",
	[SWC_SCREEN_GEOMETRY_CHANGED]         = "geometry_changed",
	[SWC_SCREEN_USABLE_GEOMETRY_CHANGED]  = "usable_geometry_changed",
};

static void evt_window(struct wl_listener *listener, void *_event)
{
	struct swc_event *event = _event;
	win_t *win = listener2win(listener);

	printf("evt_window: %p -> %p - %s\n", listener, win,
			map_window[event->type]);

	switch (event->type) {
		case SWC_WINDOW_STATE_CHANGED:
			if (win->sys->swc->state == SWC_WINDOW_STATE_NORMAL) {
				swc_window_show(win->sys->swc);
				swc_window_focus(win->sys->swc);
			}
			break;
	}
}

static void evt_screen(struct wl_listener *listener, void *_event)
{
	struct swc_event *event = _event;
	printf("evt_screen: %p - %s\n", listener,
			map_screen[event->type]);
}

static void new_window(struct swc_window *swc)
{
	printf("new_window: %p\n", swc);

	win_t *win = new0(win_t);
	win->sys   = new0(win_sys_t);
	win->sys->win = win;
	win->sys->swc = swc;
	win->sys->evt.notify = evt_window;

	wl_signal_add(&win->sys->swc->event_signal, &win->sys->evt);
}

static void new_screen(struct swc_screen *screen)
{
	printf("new_screen: %p\n", screen);

	struct wl_listener *listener = new0(struct wl_listener);
	listener->notify = evt_screen;

	wl_signal_add(&screen->event_signal, listener);
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
	static struct swc_manager manager = {
		&new_window,
		&new_screen,
	};

	printf("sys_init\n");

	/* Register log handler */
	wl_log_set_handler_server((wl_log_func_t)vprintf);

	/* Open the display */
	if (!(display = wl_display_create()))
		error("Unable to  create display");
	if (wl_display_add_socket(display, NULL) != 0)
		error("Unable to add socket");
	if (!swc_initialize(display, NULL, &manager))
		error("Unable to initialize SWC");
	if (!(events = wl_display_get_event_loop(display)))
		error("Unable to get event loop");

	/* Fail-safe key bindings */
	swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO|SWC_MOD_SHIFT, XKB_KEY_q, &cmd_exit,  NULL);
	swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO, XKB_KEY_Return, &cmd_spawn, cmd_term);
	swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO, XKB_KEY_r,      &cmd_spawn, cmd_menu);

	return new0(win_t);
}

void sys_run(win_t *root)
{
	printf("sys_run: %p\n", root);
	wl_display_run(display);
}

void sys_exit(void)
{
	printf("sys_exit\n");
	exit(0);
}

void sys_free(win_t *root)
{
	printf("sys_free: %p\n", root);
}
