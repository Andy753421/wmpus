/*
 * Copyright (c) 2015 Andy Spencer <andy753421@gmail.com>
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

#include <xcb/xcb.h>

#include "util.h"
#include "conf.h"
#include "types.h"
#include "sys.h"
#include "wm.h"

/* Configuration */
static int border     = 2;
static int stack      = 25;
static int no_capture = 0;

/* Internal structures */
struct win_sys {
};

/* Global data */
static xcb_connection_t *conn;
static list_t           *screens;

/********************
 * System functions *
 ********************/
void sys_move(win_t *win, int x, int y, int w, int h)
{
	printf("sys_move: %p - %dx%d @ %d,%d\n",
			win, w, h, x, y);
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
	printf("sys_show: %p - %d\n", win, state);
}

void sys_watch(win_t *win, event_t ev, mod_t mod)
{
	printf("sys_watch: %p - 0x%X,0x%X\n", win, ev, mod2int(mod));
}

void sys_unwatch(win_t *win, event_t ev, mod_t mod)
{
	printf("sys_unwatch: %p - 0x%X,0x%X\n", win, ev, mod2int(mod));
}

list_t *sys_info(void)
{
	printf("sys_info\n");
	if (screens == NULL) {
		win_t *screen = new0(win_t);
		screens = list_insert(NULL, screen);
	}
	return screens;
}

void sys_init(void)
{
	printf("sys_init\n");

	/* Load configuration */
	stack      = conf_get_int("main.stack",      stack);
	border     = conf_get_int("main.border",     border);
	no_capture = conf_get_int("main.no-capture", no_capture);

	/* Connect to display */
	if (!(conn = xcb_connect(NULL, NULL)))
		error("xcb connect failed");
	if (xcb_connection_has_error(conn))
		error("xcb connection has errors");
}

void sys_run(void)
{
	printf("sys_run\n");
}

void sys_exit(void)
{
	printf("sys_exit\n");
}

void sys_free(void)
{
	printf("sys_free\n");
}
