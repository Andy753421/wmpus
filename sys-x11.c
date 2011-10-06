/*
 * Copyright (c) 2011, Andy Spencer <andy753421@gmail.com>
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

#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/extensions/Xinerama.h>

#include "util.h"
#include "conf.h"
#include "sys.h"
#include "wm.h"

/* Configuration */
static int BORDER     = 2;
static int NO_CAPTURE = 0;

/* Internal structures */
struct win_sys {
	Window   xid;
	Display *dpy;
	struct {
		int left, right, top, bottom;
	} strut;
	state_t  state;
};

typedef struct {
	Key_t key;
	int   sym;
} keymap_t;

typedef enum {
	wm_proto, wm_focus, net_strut, natoms
} atom_t;

typedef enum {
	clr_focus, clr_unfocus, clr_urgent, ncolors
} color_t;

/* Global data */
static int   running;
static void *cache;
static Atom atoms[natoms];
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned long colors[ncolors];
static list_t *screens;

/* Conversion functions */
static keymap_t key2sym[] = {
	{key_left    , XK_Left },
	{key_right   , XK_Right},
	{key_up      , XK_Up   },
	{key_down    , XK_Down },
	{key_home    , XK_Home },
	{key_end     , XK_End  },
	{key_pageup  , XK_Prior},
	{key_pagedown, XK_Next },
	{key_f1      , XK_F1   },
	{key_f2      , XK_F2   },
	{key_f3      , XK_F3   },
	{key_f4      , XK_F4   },
	{key_f5      , XK_F5   },
	{key_f6      , XK_F6   },
	{key_f7      , XK_F7   },
	{key_f8      , XK_F8   },
	{key_f9      , XK_F9   },
	{key_f10     , XK_F10  },
	{key_f11     , XK_F11  },
	{key_f12     , XK_F12  },
};

/* - Modifiers */
static mod_t x2mod(unsigned int state, int up)
{
	return (mod_t){
	       .alt   = !!(state & Mod1Mask   ),
	       .ctrl  = !!(state & ControlMask),
	       .shift = !!(state & ShiftMask  ),
	       .win   = !!(state & Mod4Mask   ),
	       .up    = up,
	};
}

static unsigned int mod2x(mod_t mod)
{
	return (mod.alt   ? Mod1Mask    : 0)
	     | (mod.ctrl  ? ControlMask : 0)
	     | (mod.shift ? ShiftMask   : 0)
	     | (mod.win   ? Mod4Mask    : 0);
}

/* - Keycodes */
static Key_t x2key(KeySym sym)
{
	keymap_t *km = map_getr(key2sym,sym);
	return km ? km->key : sym;
}

static KeySym key2x(Key_t key)
{
	keymap_t *km = map_get(key2sym,key);
	return km ? km->sym : key;
}

static Key_t x2btn(int btn)
{
	return btn + key_mouse0;
}

static int btn2x(Key_t key)
{
	return key - key_mouse0;
}

/* - Pointers */
static ptr_t x2ptr(XEvent *_ev)
{
	XKeyEvent *ev = &_ev->xkey;
	return (ptr_t){ev->x, ev->y, ev->x_root, ev->y_root};
}

static Window getfocus(win_t *root, XEvent *event)
{
	int revert;
	Window focus = PointerRoot;
	if (event->type == KeyPress || event->type == KeyRelease)
		XGetInputFocus(root->sys->dpy, &focus, &revert);
	if (focus == PointerRoot)
		focus = event->xkey.subwindow;
	if (focus == None)
		focus = event->xkey.window;
	return focus;
}

/* Strut functions
 *   Struts are spaces at the edges of the screen that are used by
 *   toolbars and statusbars such as dzen. */
static int strut_copy(win_t *to, win_t *from, int scale)
{
	int left   = from->sys->strut.left;
	int right  = from->sys->strut.right;
	int top    = from->sys->strut.top;
	int bottom = from->sys->strut.bottom;
	if (left == 0 && right == 0 && top == 0 && bottom == 0)
		return 0;
	to->x += scale*(left      );
	to->y += scale*(top       );
	to->w -= scale*(left+right);
	to->h -= scale*(top+bottom);
	return 1;
}

static int strut_add(win_t *root, win_t *win)
{
	/* Get X11 strut data */
	Atom ret_type;
	int ret_size;
	unsigned long ret_items, bytes_left;
	unsigned char *xdata;
	int status = XGetWindowProperty(win->sys->dpy, win->sys->xid,
			atoms[net_strut], 0L, 4L, False, XA_CARDINAL,
			&ret_type, &ret_size, &ret_items, &bytes_left, &xdata);
	if (status != Success || ret_size != 32 || ret_items != 4)
		return 0;

	win->sys->strut.left   = ((int*)xdata)[0];
	win->sys->strut.right  = ((int*)xdata)[1];
	win->sys->strut.top    = ((int*)xdata)[2];
	win->sys->strut.bottom = ((int*)xdata)[3];
	for (list_t *cur = screens; cur; cur = cur->next)
		strut_copy(cur->data, win, 1);
	return strut_copy(root, win, 1);
}

static int strut_del(win_t *root, win_t *win)
{
	for (list_t *cur = screens; cur; cur = cur->next)
		strut_copy(cur->data, win, -1);
	return strut_copy(root, win, -1);
}

/* Window functions */
static win_t *win_new(Display *dpy, Window xid)
{
	XWindowAttributes attr;
	if (XGetWindowAttributes(dpy, xid, &attr))
		if (attr.override_redirect)
			return NULL;
	win_t *win    = new0(win_t);
	win->x        = attr.x;
	win->y        = attr.y;
	win->w        = attr.width;
	win->h        = attr.height;
	win->sys      = new0(win_sys_t);
	win->sys->dpy = dpy;
	win->sys->xid = xid;
	printf("win_new: %p = %p, %d (%d,%d %dx%d)\n",
			win, dpy, (int)xid,
			win->x, win->y, win->w, win->h);
	return win;
}

static int win_cmp(const void *_a, const void *_b)
{
	const win_t *a = _a, *b = _b;
	if (a->sys->dpy < b->sys->dpy) return -1;
	if (a->sys->dpy > b->sys->dpy) return  1;
	if (a->sys->xid < b->sys->xid) return -1;
	if (a->sys->xid > b->sys->xid) return  1;
	return 0;
}

static win_t *win_find(Display *dpy, Window xid, int create)
{
	if (!dpy || !xid)
		return NULL;
	//printf("win_find: %p, %d\n", dpy, (int)xid);
	win_sys_t sys = {.dpy=dpy, .xid=xid};
	win_t     tmp = {.sys=&sys};
	win_t **old = NULL, *new = NULL;
	if ((old = tfind(&tmp, &cache, win_cmp)))
		return *old;
	if (create && (new = win_new(dpy,xid)))
		tsearch(new, &cache, win_cmp);
	return new;
}

static void win_free(win_t *win)
{
	free(win->sys);
	free(win);
}

static void win_remove(win_t *win)
{
	tdelete(win, &cache, win_cmp);
	win_free(win);
}

static int win_viewable(win_t *win)
{
	XWindowAttributes attr;
	if (XGetWindowAttributes(win->sys->dpy, win->sys->xid, &attr))
		return attr.map_state == IsViewable;
	else
		return True;
}

/* Drawing functions */
static unsigned long get_color(Display *dpy, const char *name)
{
	XColor color;
	int screen = DefaultScreen(dpy);
	Colormap cmap = DefaultColormap(dpy, screen);
	XAllocNamedColor(dpy, cmap, name, &color, &color);
	return color.pixel;
}

/* Callbacks */
static void process_event(int type, XEvent *ev, win_t *root)
{
	Display  *dpy = root->sys->dpy;
	win_t *win = NULL;
	//printf("event: %d\n", type);

	/* Common data for all these events ... */
	ptr_t ptr = {}; mod_t mod = {};
	if (type == KeyPress    || type == KeyRelease    ||
	    type == ButtonPress || type == ButtonRelease ||
	    type == MotionNotify) {
		Window xid = getfocus(root, ev);
		if (!(win = win_find(dpy,xid,0)))
			return;
		ptr = x2ptr(ev);
		mod = x2mod(ev->xkey.state, type==KeyRelease||type==ButtonRelease);
	}

	/* Split based on event */
	if (type == KeyPress) {
		while (XCheckTypedEvent(dpy, KeyPress, ev));
		KeySym sym = XKeycodeToKeysym(dpy, ev->xkey.keycode, 0);
		printf("got key %c %hhx\n", x2key(sym), mod2int(mod));
		wm_handle_key(win, x2key(sym), mod, ptr);
	}
	else if (type == KeyRelease) {
		//printf("release: %d\n", type);
	}
	else if (type == ButtonPress) {
		if (wm_handle_key(win, x2btn(ev->xbutton.button), mod, ptr))
			XGrabPointer(dpy, ev->xbutton.root, True, PointerMotionMask|ButtonReleaseMask,
					GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
		else {
			printf("resending event\n");
			XSendEvent(win->sys->dpy, ev->xbutton.window,    True,  NoEventMask, ev);
			XSendEvent(win->sys->dpy, ev->xbutton.window,    False, NoEventMask, ev);
			XSendEvent(win->sys->dpy, ev->xbutton.root,      True,  NoEventMask, ev);
			XSendEvent(win->sys->dpy, ev->xbutton.root,      False, NoEventMask, ev);
			XSendEvent(win->sys->dpy, ev->xbutton.subwindow, True,  NoEventMask, ev);
			XSendEvent(win->sys->dpy, ev->xbutton.subwindow, False, NoEventMask, ev);
		}
	}
	else if (type == ButtonRelease) {
		XUngrabPointer(dpy, CurrentTime);
		wm_handle_key(win, x2btn(ev->xbutton.button), mod, ptr);
	}
	else if (type == MotionNotify) {
		while (XCheckTypedEvent(dpy, MotionNotify, ev));
		wm_handle_ptr(win, ptr);
	}
	else if (type == EnterNotify || type == LeaveNotify) {
		printf("enter: %d\n", type);
		key_t key = EnterNotify ? key_enter : key_leave;
		if ((win = win_find(dpy,ev->xcrossing.window,0)))
			wm_handle_key(win, key, MOD(), PTR());
	}
	else if (type == FocusIn || type == FocusOut) {
		//printf("focus: %d\n", type);
		key_t key = FocusIn ? key_focus : key_unfocus;
		if ((win = win_find(dpy,ev->xfocus.window,0)))
			wm_handle_key(win, key, MOD(), PTR());
	}
	else if (type == ConfigureNotify) {
		printf("configure: %d\n", type);
	}
	else if (type == MapNotify) {
		printf("map: %d\n", type);
	}
	else if (type == UnmapNotify) {
		if ((win = win_find(dpy,ev->xunmap.window,0)) &&
		     win->sys->state == st_show) {
			if (!strut_del(root, win))
				wm_remove(win);
			else
				wm_update();
			win->sys->state = st_hide;
		}
	}
	else if (type == DestroyNotify) {
		//printf("destroy: %d\n", type);
		if ((win = win_find(dpy,ev->xdestroywindow.window,0)))
			win_remove(win);
	}
	else if (type == ConfigureRequest) {
		XConfigureRequestEvent *cre = &ev->xconfigurerequest;
		printf("configure_req: %d - %x, (0x%lx) %dx%d @ %d,%d\n",
				type, (int)cre->window, cre->value_mask,
				cre->height, cre->width, cre->x, cre->y);
		XConfigureWindow(dpy, cre->window, cre->value_mask, &(XWindowChanges){
			.x      = cre->x,
			.y      = cre->y,
			.width  = cre->width,
			.height = cre->height,
		});

		/* This seems necessary for, but causes flicker
		 * there could be a better way to do this */
		if ((win = win_find(dpy,ev->xmaprequest.window,0)))
			sys_move(win, win->x, win->y, win->w, win->h);
	}
	else if (type == MapRequest) {
		printf("map_req: %d\n", type);
		if ((win = win_find(dpy,ev->xmaprequest.window,1))) {
			if (!strut_add(root, win))
				wm_insert(win);
			else
				wm_update();
		}
		XMapWindow(dpy,ev->xmaprequest.window);
	}
	else {
		printf("unknown event: %d\n", type);
	}
}

static int xerror(Display *dpy, XErrorEvent *err)
{
	if (err->error_code == BadWindow ||
	    (err->request_code == X_SetInputFocus     && err->error_code == BadMatch   ) ||
	    (err->request_code == X_PolyText8         && err->error_code == BadDrawable) ||
	    (err->request_code == X_PolyFillRectangle && err->error_code == BadDrawable) ||
	    (err->request_code == X_PolySegment       && err->error_code == BadDrawable) ||
	    (err->request_code == X_ConfigureWindow   && err->error_code == BadMatch   ) ||
	    (err->request_code == X_GrabButton        && err->error_code == BadAccess  ) ||
	    (err->request_code == X_GrabKey           && err->error_code == BadAccess  ) ||
	    (err->request_code == X_CopyArea          && err->error_code == BadDrawable))
		return 0;
	return xerrorxlib(dpy, err);
}

/********************
 * System functions *
 ********************/
void sys_move(win_t *win, int x, int y, int w, int h)
{
	//printf("sys_move: %p - %d,%d  %dx%d\n", win, x, y, w, h);
	int b = 2*BORDER;
	win->x = x; win->y = y;
	win->w = MAX(w,1+b); win->h = MAX(h,1+b);
	w      = MAX(w-b,1); h      = MAX(h-b,1);
	XMoveResizeWindow(win->sys->dpy, win->sys->xid, x, y, w, h);

	/* Flush events, so moving window doesn't cause re-focus
	 * There's probably a better way to do this */
	XEvent ev;
	XSync(win->sys->dpy, False);
	while (XCheckMaskEvent(win->sys->dpy, EnterWindowMask|LeaveWindowMask, &ev))
		printf("Skipping enter/leave event\n");
}

void sys_raise(win_t *win)
{
	//printf("sys_raise: %p\n", win);
	XRaiseWindow(win->sys->dpy, win->sys->xid);
}

void sys_focus(win_t *win)
{
	//printf("sys_focus: %p\n", win);

	/* Set border on focused window */
	static win_t *last = NULL;
	if (last)
		XSetWindowBorder(last->sys->dpy, last->sys->xid, colors[clr_unfocus]);
	XSetWindowBorder(win->sys->dpy, win->sys->xid, colors[clr_focus]);
	XSetWindowBorderWidth(win->sys->dpy, win->sys->xid, BORDER);
	last = win;

	/* Set actual focus */
	XSetInputFocus(win->sys->dpy, win->sys->xid,
			RevertToPointerRoot, CurrentTime);
	XSendEvent(win->sys->dpy, win->sys->xid, False, NoEventMask, &(XEvent){
		.type                 = ClientMessage,
		.xclient.window       = win->sys->xid,
		.xclient.message_type = atoms[wm_proto],
		.xclient.format       = 32,
		.xclient.data.l[0]    = atoms[wm_focus],
		.xclient.data.l[1]    = CurrentTime,
	});
}

void sys_show(win_t *win, state_t state)
{
	win->sys->state = state;
	switch (state) {
	case st_show:
		printf("sys_show: show\n");
		XMapWindow(win->sys->dpy, win->sys->xid);
		XSync(win->sys->dpy, False);
		return;
	case st_full:
		printf("sys_show: full\n");
		return;
	case st_shade:
		printf("sys_show: shade\n");
		return;
	case st_icon:
		printf("sys_show: icon\n");
		return;
	case st_hide:
		printf("sys_show: hide\n");
		XUnmapWindow(win->sys->dpy, win->sys->xid);
		return;
	}
}

void sys_watch(win_t *win, Key_t key, mod_t mod)
{
	//printf("sys_watch: %p - %x %hhx\n", win, key, mod);
	XWindowAttributes attr;
	XGetWindowAttributes(win->sys->dpy, win->sys->xid, &attr);
	long mask = attr.your_event_mask;
	if (key_mouse0 <= key && key <= key_mouse7)
		XGrabButton(win->sys->dpy, btn2x(key), mod2x(mod), win->sys->xid, False,
				mod.up ? ButtonReleaseMask : ButtonPressMask,
				GrabModeAsync, GrabModeAsync, None, None);
	else if (key == key_enter)
		XSelectInput(win->sys->dpy, win->sys->xid, EnterWindowMask|mask);
	else if (key == key_leave)
		XSelectInput(win->sys->dpy, win->sys->xid, LeaveWindowMask|mask);
	else if (key == key_focus || key == key_unfocus)
		XSelectInput(win->sys->dpy, win->sys->xid, FocusChangeMask|mask);
	else
		XGrabKey(win->sys->dpy, XKeysymToKeycode(win->sys->dpy, key2x(key)),
				mod2x(mod), win->sys->xid, True, GrabModeAsync, GrabModeAsync);
}

void sys_unwatch(win_t *win, Key_t key, mod_t mod)
{
	if (key_mouse0 <= key && key <= key_mouse7)
		XUngrabButton(win->sys->dpy, btn2x(key), mod2x(mod), win->sys->xid);
}

list_t *sys_info(win_t *win)
{
	/* Use global copy of screens so we can add struts */
	if (screens == NULL) {
		/* Add Xinerama screens */
		int n = 0;
		XineramaScreenInfo *info = NULL;
		if (XineramaIsActive(win->sys->dpy))
			info = XineramaQueryScreens(win->sys->dpy, &n);
		for (int i = 0; i < n; i++) {
			win_t *screen = new0(win_t);
			screen->x = info[i].x_org;
			screen->y = info[i].y_org;
			screen->w = info[i].width;
			screen->h = info[i].height;
			screens = list_append(screens, screen);
		}
	}
	if (screens == NULL) {
		/* No xinerama support */
		win_t *screen = new0(win_t);
		*screen = *win;
		screens = list_insert(NULL, screen);
	}
	return screens;
}

win_t *sys_init(void)
{
	Display *dpy;
	Window   xid;

	/* Load configuration */
	BORDER     = conf_get_int("main.border",     BORDER);
	NO_CAPTURE = conf_get_int("main.no-capture", NO_CAPTURE);

	/* Open the display */
	if (!(dpy = XOpenDisplay(NULL)))
		error("Unable to get display");
	if (!(xid = DefaultRootWindow(dpy)))
		error("Unable to get root window");

	/* Setup X11 data */
	atoms[wm_proto]  = XInternAtom(dpy, "WM_PROTOCOLS",  False);
	atoms[wm_focus]  = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	atoms[net_strut] = XInternAtom(dpy, "_NET_WM_STRUT", False);

	colors[clr_focus]   = get_color(dpy, "#a0a0ff");
	colors[clr_unfocus] = get_color(dpy, "#101066");
	colors[clr_urgent]  = get_color(dpy, "#ff0000");
	printf("colors = #%06lx #%06lx #%06lx\n", colors[0], colors[1], colors[2]);

	/* Select window management events */
	XSelectInput(dpy, xid, SubstructureRedirectMask|SubstructureNotifyMask);
	XSetInputFocus(dpy, None, RevertToNone, CurrentTime);
	xerrorxlib = XSetErrorHandler(xerror);

	return win_find(dpy, xid, 1);
}

void sys_run(win_t *root)
{
	/* Add each initial window */
	if (!NO_CAPTURE) {
		unsigned int nkids;
		Window par, xid, *kids = NULL;
		if (XQueryTree(root->sys->dpy, root->sys->xid,
					&par, &xid, &kids, &nkids)) {
			for(int i = 0; i < nkids; i++) {
				win_t *win = win_find(root->sys->dpy, kids[i], 1);
				if (win && win_viewable(win) && !strut_add(root,win))
					wm_insert(win);
			}
			XFree(kids);
		}
		wm_update(); // For struts
	}

	/* Main loop */
	running = 1;
	while (running)
	{
		XEvent ev;
		XNextEvent(root->sys->dpy, &ev);
		process_event(ev.type, &ev, root);
	}
}

void sys_exit(void)
{
	running = 0;
}

void sys_free(win_t *root)
{
	XCloseDisplay(root->sys->dpy);
	while (screens)
		screens = list_remove(screens, screens, 1);
	tdestroy(cache, (void(*)(void*))win_free);
}
