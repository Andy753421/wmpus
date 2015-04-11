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
#include <search.h>

#include <xcb/xcb.h>
#include <xcb/xcb_event.h>
#include <xcb/xinerama.h>

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
	xcb_window_t xcb;
	int override; // normal vs override redirect
	int managed;  // window is currently managed by wm
};

/* Global data */
static xcb_connection_t *conn;
static xcb_window_t      root;
static list_t           *screens;
static void             *cache;

/********************
 * Window functions *
 ********************/

static int win_cmp(const void *_a, const void *_b)
{
	const win_t *a = _a;
	const win_t *b = _b;
	if (a->sys->xcb < b->sys->xcb) return -1;
	if (a->sys->xcb > b->sys->xcb) return  1;
	return 0;
}

static win_t *win_get(xcb_window_t xcb)
{
	win_sys_t sys = { .xcb =  xcb };
	win_t     key = { .sys = &sys };

	win_t   **win = tfind(&key, &cache, win_cmp);

	if (!win) {
		warn("no window for %u", xcb);
		return NULL;
	}

	return *win;
}

/****************
 * XCB Wrappers *
 ****************/

static int do_query_tree(xcb_window_t win, xcb_window_t **kids)
{
	xcb_query_tree_cookie_t cookie =
		xcb_query_tree(conn, win);
	if (!cookie.sequence)
		return warn("do_query_tree: %d - bad cookie", win);

	xcb_query_tree_reply_t *reply =
		xcb_query_tree_reply(conn, cookie, NULL);
	if (!reply)
		return warn("do_query_tree: %d - no reply", win);

	int nkids = xcb_query_tree_children_length(reply);
	*kids = xcb_query_tree_children(reply);
	printf("do_query_tree: %d - n=%d\n", win, nkids);
	return nkids;
}

static int do_get_geometry(xcb_window_t win,
		int *x, int *y, int *w, int *h)
{
	xcb_get_geometry_cookie_t cookie =
		xcb_get_geometry(conn, win);
	if (!cookie.sequence)
		return warn("do_get_geometry: %d - bad cookie", win);

	xcb_get_geometry_reply_t *reply =
		xcb_get_geometry_reply(conn, cookie, NULL);
	if (!reply)
		return warn("do_get_geometry: %d - no reply", win);

	printf("do_get_geometry: %d - %dx%d @ %d,%d\n",
			win, reply->width, reply->height, reply->x, reply->y);
	*x = reply->x;
	*y = reply->y;
	*w = reply->width;
	*h = reply->height;
	return 1;
}

static int do_get_window_attributes(xcb_window_t win,
		int *override)
{
	xcb_get_window_attributes_cookie_t cookie =
		xcb_get_window_attributes(conn, win);
	if (!cookie.sequence)
		return warn("do_get_window_attributes: %d - bad cookie", win);

	xcb_get_window_attributes_reply_t *reply =
		xcb_get_window_attributes_reply(conn, cookie, NULL);
	if (!reply)
		return warn("do_get_window_attributes: %d - no reply ", win);

	printf("do_get_window_attributes: %d - %d\n",
			win, reply->override_redirect);
	*override = reply->override_redirect;
	return 1;
}

static int do_xinerama_check(void)
{
	const xcb_query_extension_reply_t *data =
		xcb_get_extension_data(conn, &xcb_xinerama_id);
	if (!data || !data->present)
		return warn("do_xinerama_check: no ext");

	xcb_xinerama_is_active_cookie_t cookie =
		xcb_xinerama_is_active(conn);
	if (!cookie.sequence)
		return warn("do_xinerama_check: no cookie");

	xcb_xinerama_is_active_reply_t *reply =
		xcb_xinerama_is_active_reply(conn, cookie, NULL);
	if (!reply)
		warn("do_xinerama_check: no reply");

	printf("do_xinerama_check: %d\n", reply->state);
	return reply && reply->state;
}

static int do_query_screens(xcb_xinerama_screen_info_t **info)
{
	xcb_xinerama_query_screens_cookie_t cookie =
		xcb_xinerama_query_screens(conn);
	if (!cookie.sequence)
		return warn("do_query_screens: bad cookie");

	xcb_xinerama_query_screens_reply_t *reply =
		xcb_xinerama_query_screens_reply(conn, cookie, NULL);
	if (!reply)
		return warn("do_query_screens: no reply");

	int ninfo = xcb_xinerama_query_screens_screen_info_length(reply);
	*info = xcb_xinerama_query_screens_screen_info(reply);
	printf("do_query_screens: %d screens\n", ninfo);
	return ninfo;
}

/**********************
 * X11 Event Handlers *
 **********************/

/* Specific events */
static void on_create_notify(xcb_create_notify_event_t *event)
{
	win_t     *win = new0(win_t);
	win_sys_t *sys = new0(win_sys_t);

	printf("on_create_notify:     xcb=%u -> win=%p\n",
			event->window, win);

	win->x        = event->x;
	win->y        = event->y;
	win->w        = event->width;
	win->h        = event->height;
	win->sys      = sys;

	sys->xcb      = event->window;
	sys->override = event->override_redirect;

	tsearch(win, &cache, win_cmp);
}

static void on_destroy_notify(win_t *win, xcb_destroy_notify_event_t *event)
{
	printf("on_destroy_notify:    xcb=%u -> win=%p\n",
			event->window, win);

	tdelete(win, &cache, win_cmp);

	free(win->sys);
	free(win);
}

static void on_map_request(win_t *win, xcb_map_request_event_t *event)
{
	printf("on_map_request:       xcb=%u -> win=%p\n",
			event->window, win);

	if (!win->sys->managed) {
		wm_insert(win);
		win->sys->managed = 1;
	}

	xcb_map_window(conn, win->sys->xcb);
	sys_move(win, win->x, win->y, win->w, win->h);
}

static void on_configure_request(win_t *win, xcb_configure_request_event_t *event)
{
	printf("on_configure_request: xcb=%u -> win=%p -- %dx%d @ %d,%d\n",
			event->window, win,
			event->width, event->height,
			event->x, event->y);

	win->x = event->x;
	win->y = event->y;
	win->w = event->width;
	win->h = event->height;

	xcb_configure_notify_event_t resp = {
		.response_type = XCB_CONFIGURE_NOTIFY,
		.event         = win->sys->xcb,
		.window        = win->sys->xcb,
		.x             = win->x,
		.y             = win->y,
		.width         = win->w,
		.height        = win->h,
		.border_width  = border,
	};

	xcb_send_event(conn, 0, win->sys->xcb,
			XCB_EVENT_MASK_STRUCTURE_NOTIFY,
			(const char *)&resp);
}

/* Generic Event */
static void on_event(xcb_generic_event_t *event)
{
	win_t *win = NULL;

	int type = XCB_EVENT_RESPONSE_TYPE(event);
	int sent = XCB_EVENT_SENT(event);
	const char *name = NULL;

	switch (type) {
		case XCB_CREATE_NOTIFY:
			on_create_notify((xcb_create_notify_event_t *)event);
			break;
		case XCB_DESTROY_NOTIFY:
			if ((win = win_get(((xcb_destroy_notify_event_t *)event)->window)))
				on_destroy_notify(win, (xcb_destroy_notify_event_t *)event);
			break;
		case XCB_MAP_REQUEST:
			if ((win = win_get(((xcb_map_request_event_t *)event)->window)))
				on_map_request(win, (xcb_map_request_event_t *)event);
			break;
		case XCB_CONFIGURE_REQUEST:
			if ((win = win_get(((xcb_configure_request_event_t *)event)->window)))
				on_configure_request(win, (xcb_configure_request_event_t *)event);
			break;
		default:
			name = xcb_event_get_label(type);
			printf("on_event: %d:%02X -> %s\n",
				!!sent, type, name?:"unknown_event");
			break;
	}
}

/********************
 * System functions *
 ********************/

void sys_move(win_t *win, int x, int y, int w, int h)
{
	printf("sys_move:  %p - %dx%d @ %d,%d\n",
			win, w, h, x, y);

	win->x = x;
	win->y = y;
	win->w = w;
	win->h = h;

	uint16_t mask   = XCB_CONFIG_WINDOW_X
		        | XCB_CONFIG_WINDOW_Y
		        | XCB_CONFIG_WINDOW_WIDTH
		        | XCB_CONFIG_WINDOW_HEIGHT;
	uint32_t list[] = {x, y, w, h};

	xcb_configure_window(conn, win->sys->xcb, mask, list);
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
	printf("sys_show:  %p - %d\n", win, state);
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

	if (screens == NULL && do_xinerama_check()) {
		/* Add Xinerama screens */
		xcb_xinerama_screen_info_t *info = NULL;
		int ninfo = do_query_screens(&info);
		for (int i = 0; i < ninfo; i++) {
			win_t *screen = new0(win_t);

			screen->x = info[i].x_org;
			screen->y = info[i].y_org;
			screen->w = info[i].width;
			screen->h = info[i].height;

			screens = list_insert(NULL, screen);

			printf("sys_info: xinerama screen - %dx%d @ %d,%d\n",
					screen->w, screen->h,
					screen->x, screen->y);
		}
	}

	if (screens == NULL) {
		/* No xinerama support */
		const xcb_setup_t *setup = xcb_get_setup(conn);
		xcb_screen_t      *geom  = xcb_setup_roots_iterator(setup).data;

		win_t *screen = new0(win_t);

		screen->w = geom->width_in_pixels;
		screen->h = geom->height_in_pixels;

		screens = list_insert(NULL, screen);

		printf("sys_info: root screen - %dx%d\n",
				screen->w, screen->h);
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

	/* Get root window */
	const xcb_setup_t     *setup = xcb_get_setup(conn);
	xcb_screen_iterator_t  iter  = xcb_setup_roots_iterator(setup);
	root = iter.data->root;

	/* Request substructure redirect */
	xcb_event_mask_t     mask;
	xcb_void_cookie_t    cookie;
	xcb_generic_error_t *err;
	mask   = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
		 XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;
	cookie = xcb_change_window_attributes_checked(conn, root,
			XCB_CW_EVENT_MASK, &mask);
	if ((err = xcb_request_check(conn, cookie)))
		error("Another window manager is already running");
}

void sys_run(void)
{
	printf("sys_run\n");

	/* Add each initial window */
	if (!no_capture) {
		xcb_window_t *kids = NULL;
		int nkids = do_query_tree(root, &kids);
		for(int i = 0; i < nkids; i++) {
			win_t *win = new0(win_t);
			win->sys = new0(win_sys_t);
			win->sys->xcb = kids[i];
			do_get_geometry(kids[i], &win->x, &win->y, &win->w, &win->h);
			do_get_window_attributes(kids[i], &win->sys->override);
			tsearch(win, &cache, win_cmp);
			if (!win->sys->override) {
				wm_insert(win);
				win->sys->managed = 1;
			}
		}
		xcb_flush(conn);
	}

	/* Main loop */
	while (1)
	{
		int status;
		xcb_generic_event_t *event;
		if (!(event = xcb_wait_for_event(conn)))
			break;
		on_event(event);
		free(event);
		if (!(status = xcb_flush(conn)))
			break;
	}
}

void sys_exit(void)
{
	printf("sys_exit\n");
	if (conn)
		xcb_disconnect(conn);
	conn = NULL;
}

void sys_free(void)
{
	printf("sys_free\n");
	if (conn)
		xcb_disconnect(conn);
	conn = NULL;
}
