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
#include <string.h>

#include <xcb/xcb.h>
#include <xcb/xcb_event.h>
#include <xcb/xcb_keysyms.h>
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
	xcb_window_t     xcb;    // xcb window id
	xcb_event_mask_t events; // currently watch events
	int override;            // normal vs override redirect
	int mapped;              // window is currently mapped
};

typedef enum {
	CLR_FOCUS,
	CLR_UNFOCUS,
	CLR_URGENT,
	NCOLORS
} color_t;

/* Global data */
static xcb_connection_t  *conn;
static xcb_key_symbols_t *keysyms;
static xcb_colormap_t     colormap;
static xcb_window_t       root;
static xcb_event_mask_t   events;
static list_t            *screens;
static void              *cache;
static xcb_pixmap_t       colors[NCOLORS];
static unsigned int       grabbed;

/************************
 * Conversion functions *
 ************************/

/* Key presses */
static struct {
	event_t      ev;
	xcb_keysym_t sym;
} keysym_map[] = {
	{ EV_LEFT,     0xFF51 },
	{ EV_RIGHT,    0xFF53 },
	{ EV_UP,       0xFF52 },
	{ EV_DOWN,     0xFF54 },
	{ EV_HOME,     0xFF50 },
	{ EV_END,      0xFF57 },
	{ EV_PAGEUP,   0xFF55 },
	{ EV_PAGEDOWN, 0xFF56 },
	{ EV_F1,       0xFFBE },
	{ EV_F2,       0xFFBF },
	{ EV_F3,       0xFFC0 },
	{ EV_F4,       0xFFC1 },
	{ EV_F5,       0xFFC2 },
	{ EV_F6,       0xFFC3 },
	{ EV_F7,       0xFFC4 },
	{ EV_F8,       0xFFC5 },
	{ EV_F9,       0xFFC6 },
	{ EV_F10,      0xFFC7 },
	{ EV_F11,      0xFFC8 },
	{ EV_F12,      0xFFC9 },
};

static xcb_keycode_t *event_to_keycodes(event_t ev)
{
	xcb_keycode_t *codes = NULL;

	/* Get keysym */
	xcb_keysym_t keysym = map_get(keysym_map, ev, ev, sym, ev);

	/* Get keycodes */
	if (!(codes = xcb_key_symbols_get_keycode(keysyms, keysym)))
		warn("no keycode found for %d->%d", ev, keysym);

	return codes;
}

static event_t keycode_to_event(xcb_keycode_t code)
{
	/* Get keysym */
	xcb_keysym_t keysym = xcb_key_symbols_get_keysym(keysyms, code, 0);

	/* Get event */
	return map_get(keysym_map, sym,keysym, ev,keysym);
}

/* Button presses */
static event_t button_to_event(xcb_button_t btn)
{
	return EV_MOUSE0 + btn;
}

static xcb_button_t event_to_button(event_t ev)
{
	return ev - EV_MOUSE0;
}

/* Modifier masks */
static xcb_mod_mask_t mod_to_mask(mod_t mod)
{
	xcb_mod_mask_t mask = 0;
	if (mod.alt)   mask |= XCB_MOD_MASK_1;
	if (mod.ctrl)  mask |= XCB_MOD_MASK_CONTROL;
	if (mod.shift) mask |= XCB_MOD_MASK_SHIFT;
	if (mod.win)   mask |= XCB_MOD_MASK_4;
	return mask;
}

static mod_t mask_to_mod(xcb_mod_mask_t mask, int up)
{
	mod_t mod = { .up = up };
	if (mask & XCB_MOD_MASK_1)       mod.alt   = 1;
	if (mask & XCB_MOD_MASK_CONTROL) mod.ctrl  = 1;
	if (mask & XCB_MOD_MASK_SHIFT)   mod.shift = 1;
	if (mask & XCB_MOD_MASK_4)       mod.win   = 1;
	return mod;
}

/* Mouse pointers */
static ptr_t list_to_ptr(int16_t *list)
{
	ptr_t ptr = {};
	if (list) {
		ptr.rx = list[0]; // root_x
		ptr.ry = list[1]; // root_y
		ptr.x  = list[2]; // event_x
		ptr.y  = list[3]; // event_y
	}
	return ptr;
}

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
		int *override, int *mapped)
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
	*mapped   = reply->map_state != XCB_MAP_STATE_UNMAPPED;
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

static xcb_pixmap_t do_alloc_color(uint32_t rgb)
{
	uint16_t r = (rgb & 0xFF0000) >> 8;
	uint16_t g = (rgb & 0x00FF00);
	uint16_t b = (rgb & 0x0000FF) << 8;
	xcb_alloc_color_cookie_t cookie =
		xcb_alloc_color(conn, colormap, r, g, b);
	if (!cookie.sequence)
		return warn("do_alloc_color: bad cookie");

	xcb_alloc_color_reply_t *reply =
		xcb_alloc_color_reply(conn, cookie, NULL);
	if (!reply)
		return warn("do_alloc_color: no reply");

	printf("do_alloc_color: %06x -> %06x\n", rgb, reply->pixel);
	return reply->pixel;
}

static void do_grab_pointer(xcb_event_mask_t mask)
{
	if (!grabbed)
		xcb_grab_pointer(conn, 0, root, mask,
				XCB_GRAB_MODE_ASYNC,
				XCB_GRAB_MODE_ASYNC,
				0, 0, XCB_CURRENT_TIME);
	grabbed++;
}

static void do_ungrab_pointer(void)
{
	grabbed--;
	if (!grabbed)
		xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
}

/**************************
 * Window Manager Helpers *
 **************************/

/* Send event info */
static int send_event(event_t ev, xcb_window_t ewin)
{
	win_t *win = win_get(ewin);
	do_grab_pointer(0);
	int status = wm_handle_event(win, ev, MOD(), PTR());
	do_ungrab_pointer();
	return status;
}

/* Send event info */
static int send_event_info(event_t ev, xcb_mod_mask_t mask, int up, int16_t *pos,
		xcb_window_t rwin, xcb_window_t ewin, xcb_window_t cwin)
{
	xcb_window_t xcb = ewin == rwin ? cwin : ewin;
	win_t *win = win_get(xcb);
	mod_t  mod = mask_to_mod(mask, up);
	ptr_t  ptr = list_to_ptr(pos);
	do_grab_pointer(0);
	int status = wm_handle_event(win, ev, mod, ptr);
	do_ungrab_pointer();
	return status;
}

/* Send pointer motion info */
static int send_pointer(int16_t *pos,
		xcb_window_t rwin, xcb_window_t ewin, xcb_window_t cwin)
{
	xcb_window_t xcb = ewin == rwin ? cwin : ewin;
	win_t *win = win_get(xcb);
	ptr_t  ptr = list_to_ptr(pos);
	do_grab_pointer(0);
	int status = wm_handle_ptr(win, ptr);
	do_ungrab_pointer();
	return status;
}

/* Send window state info */
static int send_state(void)
{
	return 0;
}


/**********************
 * X11 Event Handlers *
 **********************/

/* Specific events */
static void on_key_event(xcb_key_press_event_t *event, int up)
{
	printf("on_key_event:         xcb=%-8u\n", event->event);
	event_t ev = keycode_to_event(event->detail);
	send_event_info(ev, event->state, up, &event->root_x,
		event->root, event->event, event->child);
}

static void on_button_event(xcb_button_press_event_t *event, int up)
{
	printf("on_button_event:      xcb=%-8u\n", event->event);
	event_t ev = button_to_event(event->detail);
	if (!send_event_info(ev, event->state, up, &event->root_x,
				event->root, event->event, event->child))
		xcb_allow_events(conn, XCB_ALLOW_REPLAY_POINTER, event->time);
	else if (!up)
		do_grab_pointer(XCB_EVENT_MASK_POINTER_MOTION |
		                XCB_EVENT_MASK_BUTTON_RELEASE);
	else
		do_ungrab_pointer();

}

static void on_motion_notify(xcb_motion_notify_event_t *event)
{
	printf("on_motion_notify:     xcb=%-8u - %d,%d / %d.%d\n", event->event,
			event->event_x, event->event_y,
			event->root_x,  event->root_y);
	send_pointer(&event->root_x, event->root, event->event, event->child);
}

static void on_enter_notify(xcb_enter_notify_event_t *event)
{
	if (event->mode != XCB_NOTIFY_MODE_NORMAL)
		return;
	printf("on_enter_notify:      xcb=%-8u\n", event->event);
	send_event_info(EV_ENTER, event->state, 0, &event->root_x,
		event->root, event->event, event->child);
}

static void on_leave_notify(xcb_leave_notify_event_t *event)
{
	if (event->mode != XCB_NOTIFY_MODE_NORMAL)
		return;
	printf("on_leave_notify:      xcb=%-8u\n", event->event);
	send_event_info(EV_LEAVE, event->state, 0, &event->root_x,
		event->root, event->event, event->child);
}

static void on_focus_in(xcb_focus_in_event_t *event)
{
	if (event->mode != XCB_NOTIFY_MODE_NORMAL &&
	    event->mode != XCB_NOTIFY_MODE_WHILE_GRABBED)
		return;
	printf("on_focus_in:          xcb=%-8u mode=%d\n", event->event, event->mode);
	xcb_change_window_attributes(conn, event->event,
			XCB_CW_BORDER_PIXEL, &colors[CLR_FOCUS]);
	if (event->mode == XCB_NOTIFY_MODE_NORMAL)
		send_event(EV_FOCUS, event->event);
}

static void on_focus_out(xcb_focus_out_event_t *event)
{
	if (event->mode != XCB_NOTIFY_MODE_NORMAL &&
	    event->mode != XCB_NOTIFY_MODE_WHILE_GRABBED)
		return;
	printf("on_focus_out:         xcb=%-8u mode=%d\n", event->event, event->mode);
	xcb_change_window_attributes(conn, event->event,
			XCB_CW_BORDER_PIXEL, &colors[CLR_UNFOCUS]);
	if (event->mode == XCB_NOTIFY_MODE_NORMAL)
		send_event(EV_UNFOCUS, event->event);
}

static void on_create_notify(xcb_create_notify_event_t *event)
{
	win_t     *win = new0(win_t);
	win_sys_t *sys = new0(win_sys_t);

	printf("on_create_notify:     xcb=%-8u -> win=%p\n",
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
	printf("on_destroy_notify:    xcb=%-8u -> win=%p\n",
			event->window, win);

	tdelete(win, &cache, win_cmp);

	free(win->sys);
	free(win);
}

static void on_map_request(win_t *win, xcb_map_request_event_t *event)
{
	printf("on_map_request:       xcb=%-8u -> win=%p\n",
			event->window, win);

	if (!win->sys->override && !win->sys->mapped)
		wm_insert(win);
	win->sys->mapped = 1;

	xcb_map_window(conn, win->sys->xcb);
	sys_move(win, win->x, win->y, win->w, win->h);
}

static void on_configure_request(win_t *win, xcb_configure_request_event_t *event)
{
	printf("on_configure_request: xcb=%-8u -> win=%p -- %dx%d @ %d,%d\n",
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
	win_t   *win = NULL;

	int type = XCB_EVENT_RESPONSE_TYPE(event);
	int sent = XCB_EVENT_SENT(event);
	const char *name = NULL;

	switch (type) {
		/* Input handling */
		case XCB_KEY_PRESS:
			on_key_event((xcb_key_press_event_t *)event, 0);
			break;
		case XCB_KEY_RELEASE:
			on_key_event((xcb_key_release_event_t *)event, 1);
			break;
		case XCB_BUTTON_PRESS:
			on_button_event((xcb_button_press_event_t *)event, 0);
			break;
		case XCB_BUTTON_RELEASE:
			on_button_event((xcb_button_release_event_t *)event, 1);
			break;
		case XCB_MOTION_NOTIFY:
			on_motion_notify((xcb_motion_notify_event_t *)event);
			break;
		case XCB_ENTER_NOTIFY:
			on_enter_notify((xcb_enter_notify_event_t *)event);
			break;
		case XCB_LEAVE_NOTIFY:
			on_leave_notify((xcb_leave_notify_event_t *)event);
			break;
		case XCB_FOCUS_IN:
			on_focus_in((xcb_focus_in_event_t *)event);
			break;
		case XCB_FOCUS_OUT:
			on_focus_out((xcb_focus_out_event_t *)event);
			break;

		/* Window management */
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

		/* Unknown events */
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

	int b = 2*border;

	win->x = x;
	win->y = y;
	win->w = MAX(w,1+b);
	win->h = MAX(h,1+b);
	w      = MAX(w-b,1);
	h      = MAX(h-b,1);

	uint16_t mask   = XCB_CONFIG_WINDOW_X
		        | XCB_CONFIG_WINDOW_Y
		        | XCB_CONFIG_WINDOW_WIDTH
		        | XCB_CONFIG_WINDOW_HEIGHT
			| XCB_CONFIG_WINDOW_BORDER_WIDTH;
	uint32_t list[] = {x, y, w, h, border};

	xcb_configure_window(conn, win->sys->xcb, mask, list);
}

void sys_raise(win_t *win)
{
	printf("sys_raise: %p\n", win);
	xcb_circulate_window(conn, XCB_CIRCULATE_RAISE_LOWEST, win->sys->xcb);
}

void sys_focus(win_t *win)
{
	printf("sys_focus: %p\n", win);
	xcb_window_t xcb = win ? win->sys->xcb : root;

	xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT,
			xcb, XCB_CURRENT_TIME);
}

void sys_show(win_t *win, state_t state)
{
	printf("sys_show:  %p - %d\n", win, state);
}

void sys_watch(win_t *win, event_t ev, mod_t mod)
{
	printf("sys_watch: %p - 0x%X,0x%X\n", win, ev, mod2int(mod));
	xcb_window_t      xcb  = win ? win->sys->xcb     : root;
	xcb_event_mask_t *mask = win ? &win->sys->events : &events;
	xcb_mod_mask_t    mods = 0;
	xcb_button_t      btn  = 0;
	xcb_keycode_t    *code = 0;

	switch (ev) {
		case EV_ENTER:
			*mask |= XCB_EVENT_MASK_ENTER_WINDOW;
			xcb_change_window_attributes(conn, xcb, XCB_CW_EVENT_MASK, mask);
			break;

		case EV_LEAVE:
			*mask |= XCB_EVENT_MASK_LEAVE_WINDOW;
			xcb_change_window_attributes(conn, xcb, XCB_CW_EVENT_MASK, mask);
			break;

		case EV_FOCUS:
		case EV_UNFOCUS:
			*mask |= XCB_EVENT_MASK_FOCUS_CHANGE;
			xcb_change_window_attributes(conn, xcb, XCB_CW_EVENT_MASK, mask);
			break;

		case EV_MOUSE0...EV_MOUSE7:
			btn    = event_to_button(ev);
			mods   = mod_to_mask(mod);
			*mask |= mod.up ? XCB_EVENT_MASK_BUTTON_RELEASE
			                : XCB_EVENT_MASK_BUTTON_PRESS;
			xcb_grab_button(conn, 0, xcb, *mask,
					XCB_GRAB_MODE_ASYNC,
					XCB_GRAB_MODE_ASYNC,
					0, 0, btn, mods);
			break;

		default:
			code = event_to_keycodes(ev);
			mods = mod_to_mask(mod);
			for (int i = 0; code && code[i] != XCB_NO_SYMBOL; i++)
				xcb_grab_key(conn, 1, xcb, mods, code[i],
						XCB_GRAB_MODE_ASYNC,
						XCB_GRAB_MODE_ASYNC);

			break;
	}
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
	root     = iter.data->root;
	colormap = iter.data->default_colormap;

	/* Allocate key symbols */
	if (!(keysyms = xcb_key_symbols_alloc(conn)))
		error("cannot allocate key symbols");

	/* Read color information */
	colors[CLR_FOCUS]   = do_alloc_color(0xFF6060);
	colors[CLR_UNFOCUS] = do_alloc_color(0xD8D8FF);
	colors[CLR_URGENT]  = do_alloc_color(0xFF0000);

	/* Request substructure redirect */
	xcb_void_cookie_t cookie;
	xcb_generic_error_t *err;
	events = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
		 XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;
	cookie = xcb_change_window_attributes_checked(conn, root,
			XCB_CW_EVENT_MASK, &events);
	if ((err = xcb_request_check(conn, cookie)))
		error("another window manager is already running");
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
			do_get_window_attributes(kids[i],
				&win->sys->override, &win->sys->mapped);
			tsearch(win, &cache, win_cmp);
			if (!win->sys->override && win->sys->mapped)
				wm_insert(win);
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
