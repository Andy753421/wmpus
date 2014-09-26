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
	struct swc_screen  *ss;
	struct swc_window  *sw;
};

/* Global data */
static struct wl_display    *display;
static struct wl_event_loop *events;

/*******************
 * Debug functions *
 *******************/
static const char * cmd_term[] = { "st-wl",        NULL };
static const char * cmd_menu[] = { "dmenu_run-wl", NULL };

static void cmd_exit(void *data, uint32_t time, uint32_t value, uint32_t state)
{
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
		return;

	printf("cmd_exit\n");
	sys_exit();
}

static void cmd_spawn(void *data, uint32_t time, uint32_t value, uint32_t state)
{
	char **cmd = data;
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
		return;

	printf("cmd_spawn: %s\n", cmd[0]);
	if (fork() == 0) {
		close(2);
		execvp(cmd[0], cmd);
		exit(0);
	}
}

/*************
 * Callbacks *
 *************/

/* Screen callbacks */
static void screen_entered(void *data)
{
	printf("screen_entered: %p\n", data);
}

static void screen_geometry(void *data)
{
	printf("screen_geometry: %p\n", data);
}

static void screen_usable(void *data)
{
	printf("screen_usable: %p\n", data);
}

static void screen_destroy(void *data)
{
	printf("screen_destroy: %p\n", data);
}

static const struct swc_screen_handler screen_handler = {
	.entered                 = &screen_entered,
	.geometry_changed        = &screen_geometry,
	.usable_geometry_changed = &screen_usable,
	.destroy                 = &screen_destroy,
};

/* Window callbacks */
static void window_entered(void *_win)
{
	win_t *win = _win;
	printf("window_entered: %p\n", win);
	swc_window_show(win->sys->sw);
	swc_window_focus(win->sys->sw);
}

static void window_title(void *_win)
{
	win_t *win = _win;
	printf("window_title: %p\n", win);
}

static void window_class(void *_win)
{
	win_t *win = _win;
	printf("window_class: %p\n", win);
}

static void window_parent(void *_win)
{
	win_t *win = _win;
	printf("window_parent: %p\n", win);
}

static void window_destroy(void *_win)
{
	win_t *win = _win;
	printf("window_destroy: %p\n", win);
}

static const struct swc_window_handler window_handler = {
	.entered        = &window_entered,
	.title_changed  = &window_title,
	.class_changed  = &window_class,
	.parent_changed = &window_parent,
	.destroy        = &window_destroy,
};

/* System callbacks */
static void new_screen(struct swc_screen *ss)
{
	printf("new_screen: %p\n", ss);

	win_t *win = new0(win_t);
	win->sys   = new0(win_sys_t);
	win->sys->win = win;
	win->sys->ss  = ss;

	swc_screen_set_handler(ss, &screen_handler, win);
}

static void new_window(struct swc_window *sw)
{
	printf("new_window: %p\n", sw);

	win_t *win = new0(win_t);
	win->sys   = new0(win_sys_t);
	win->sys->win = win;
	win->sys->sw  = sw;

	swc_window_set_handler(sw, &window_handler, win);
	swc_window_set_tiled(NULL);
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
		.new_screen = &new_screen,
		.new_window = &new_window,
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
	swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO|SWC_MOD_SHIFT, XKB_KEY_q,
			&cmd_exit,  NULL);
	swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO, XKB_KEY_Return,
			&cmd_spawn, cmd_term);
	swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO, XKB_KEY_r,
			&cmd_spawn, cmd_menu);

	return new0(win_t);
}

void sys_run(win_t *root)
{
	printf("sys_run: %p\n", root);
	wl_display_run(display);
	wl_display_destroy(display);
}

void sys_exit(void)
{
	printf("sys_exit\n");
	wl_display_terminate(display);
}

void sys_free(win_t *root)
{
	printf("sys_free: %p\n", root);
}
