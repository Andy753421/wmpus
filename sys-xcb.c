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

/*****************
 * Constant data *
 *****************/

const char *event_names[] = {
	[XCB_KEY_PRESS        ] "key_press",         [XCB_KEY_RELEASE      ] "key_release",
	[XCB_BUTTON_PRESS     ] "button_press",      [XCB_BUTTON_RELEASE   ] "button_release",
	[XCB_MOTION_NOTIFY    ] "motion_notify",     [XCB_ENTER_NOTIFY     ] "enter_notify",
	[XCB_LEAVE_NOTIFY     ] "leave_notify",      [XCB_FOCUS_IN         ] "focus_in",
	[XCB_FOCUS_OUT        ] "focus_out",         [XCB_KEYMAP_NOTIFY    ] "keymap_notify",
	[XCB_EXPOSE           ] "expose",            [XCB_GRAPHICS_EXPOSURE] "graphics_exposure",
	[XCB_NO_EXPOSURE      ] "no_exposure",       [XCB_VISIBILITY_NOTIFY] "visibility_notify",
	[XCB_CREATE_NOTIFY    ] "create_notify",     [XCB_DESTROY_NOTIFY   ] "destroy_notify",
	[XCB_UNMAP_NOTIFY     ] "unmap_notify",      [XCB_MAP_NOTIFY       ] "map_notify",
	[XCB_MAP_REQUEST      ] "map_request",       [XCB_REPARENT_NOTIFY  ] "reparent_notify",
	[XCB_CONFIGURE_NOTIFY ] "configure_notify",  [XCB_CONFIGURE_REQUEST] "configure_request",
	[XCB_GRAVITY_NOTIFY   ] "gravity_notify",    [XCB_RESIZE_REQUEST   ] "resize_request",
	[XCB_CIRCULATE_NOTIFY ] "circulate_notify",  [XCB_CIRCULATE_REQUEST] "circulate_request",
	[XCB_PROPERTY_NOTIFY  ] "property_notify",   [XCB_SELECTION_CLEAR  ] "selection_clear",
	[XCB_SELECTION_REQUEST] "selection_request", [XCB_SELECTION_NOTIFY ] "selection_notify",
	[XCB_COLORMAP_NOTIFY  ] "colormap_notify",   [XCB_CLIENT_MESSAGE   ] "client_message",
	[XCB_MAPPING_NOTIFY   ] "mapping_notify",    [XCB_GE_GENERIC       ] "ge_generic",
};

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
		printf("Warning: no window for %u\n", xcb);
		return NULL;
	}

	return *win;
}

/****************
 * XCB Wrappers *
 ****************/

static xcb_query_tree_reply_t *do_query_tree(xcb_window_t win)
{
	xcb_query_tree_cookie_t cookie =
		xcb_query_tree(conn, win);
	xcb_query_tree_reply_t *reply =
		xcb_query_tree_reply(conn, cookie, NULL);
	if (!reply)
		error("do_query_tree: %d - no reply", win);
	printf("do_query_tree: %d\n", win);
	return reply;
}

static xcb_get_geometry_reply_t *do_get_geometry(xcb_window_t win)
{
	xcb_get_geometry_cookie_t cookie =
		xcb_get_geometry(conn, win);
	xcb_get_geometry_reply_t *reply =
		xcb_get_geometry_reply(conn, cookie, NULL);
	if (!reply)
		error("do_get_geometry: %d - no reply", win);
	printf("do_get_geometry: %d - %dx%d @ %d,%d\n",
			win, reply->width, reply->height, reply->x, reply->y);
	return reply;
}

static xcb_get_window_attributes_reply_t *do_get_window_attributes(xcb_window_t win)
{
	xcb_get_window_attributes_cookie_t cookie =
		xcb_get_window_attributes(conn, win);
	xcb_get_window_attributes_reply_t *reply =
		xcb_get_window_attributes_reply(conn, cookie, NULL);
	if (!reply)
		error("do_get_window_attributes: %d - no reply ", win);
	printf("do_get_window_attributes: %d - %d\n",
			win, reply->override_redirect);
	return reply;
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
	switch (event->response_type) {
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
			printf("on_%s\n", event_names[event->response_type] ?: "unknown_event");
			break;
	}
}

/********************
 * System functions *
 ********************/

void sys_move(win_t *win, int x, int y, int w, int h)
{
	printf("sys_move: %p - %dx%d @ %d,%d\n",
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
		xcb_query_tree_reply_t *tree;
		unsigned int nkids;
		xcb_window_t *kids;

		tree  = do_query_tree(root);
		nkids = xcb_query_tree_children_length(tree);
		kids  = xcb_query_tree_children(tree);

		for(int i = 0; i < nkids; i++) {
			xcb_get_geometry_reply_t *geom;
			xcb_get_window_attributes_reply_t *attr;

			geom = do_get_geometry(kids[i]);
			attr = do_get_window_attributes(kids[i]);

			win_t     *win = new0(win_t);
			win_sys_t *sys = new0(win_sys_t);

			win->x        = geom->x;
			win->y        = geom->y;
			win->w        = geom->width;
			win->h        = geom->height;
			win->sys      = sys;

			sys->xcb      = kids[i];
			sys->override = attr->override_redirect;

			tsearch(win, &cache, win_cmp);

			if (!attr->override_redirect) {
				wm_insert(win);
				sys->managed = 1;
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
