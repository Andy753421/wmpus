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

#include "util.h"
#include "conf.h"
#include "types.h"
#include "sys.h"
#include "wm.h"

/* Internal structures */
struct win_sys {
	win_t *win;
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

static void new_window(void)
{
	printf("new_window\n");
}

static void new_screen(void)
{
	printf("new_screen\n");
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

list_t *sys_info(void)
{
	static win_t root;
	printf("sys_info\n");
	return list_insert(NULL, &root);
}

void sys_init(void)
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

	/* Add input devices */
	(void)cmd_term;
	(void)cmd_menu;
	(void)cmd_exit;
	(void)cmd_spawn;
	(void)new_window;
	(void)new_screen;
}

void sys_run(void)
{
	printf("sys_run\n");
	wl_display_run(display);
}

void sys_exit(void)
{
	printf("sys_exit\n");
	exit(0);
}

void sys_free(void)
{
	printf("sys_free\n");
}
