/*
 * Copyright (c) 2011-2012, Andy Spencer <andy753421@gmail.com>
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
static int border     = 2;
static int no_capture = 0;

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
	event_t ev;
	int     sym;
} event_map_t;

typedef enum {
	WM_PROTO, WM_FOCUS, NET_STRUT, NATOMS
} atom_t;

typedef enum {
	CLR_FOCUS, CLR_UNFOCUS, CLR_URGENT, NCOLORS
} color_t;

/* Global data */
static int   running;
static void *cache;
static Atom atoms[NATOMS];
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned long colors[NCOLORS];
static list_t *screens;
static list_t *struts;

/* Conversion functions */
static event_map_t ev2sym[] = {
	{EV_LEFT    , XK_Left },
	{EV_RIGHT   , XK_Right},
	{EV_UP      , XK_Up   },
	{EV_DOWN    , XK_Down },
	{EV_HOME    , XK_Home },
	{EV_END     , XK_End  },
	{EV_PAGEUP  , XK_Prior},
	{EV_PAGEDOWN, XK_Next },
	{EV_F1      , XK_F1   },
	{EV_F2      , XK_F2   },
	{EV_F3      , XK_F3   },
	{EV_F4      , XK_F4   },
	{EV_F5      , XK_F5   },
	{EV_F6      , XK_F6   },
	{EV_F7      , XK_F7   },
	{EV_F8      , XK_F8   },
	{EV_F9      , XK_F9   },
	{EV_F10     , XK_F10  },
	{EV_F11     , XK_F11  },
	{EV_F12     , XK_F12  },
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
static event_t xk2ev(KeySym sym)
{
	event_map_t *em = map_getr(ev2sym,sym);
	return em ? em->ev : sym;
}

static KeySym ev2xk(event_t ev)
{
	event_map_t *em = map_get(ev2sym,ev);
	return em ? em->sym : ev;
}

static event_t xb2ev(int btn)
{
	return btn + EV_MOUSE0;
}

static int ev2xb(event_t ev)
{
	return ev - EV_MOUSE0;
}

/* - Pointers */
static ptr_t x2ptr(XEvent *xe)
{
	XKeyEvent *xke = &xe->xkey;
	return (ptr_t){xke->x, xke->y, xke->x_root, xke->y_root};
}

static Window getfocus(win_t *root, XEvent *xe)
{
	int revert;
	Window focus = PointerRoot;
	if (xe->type == KeyPress || xe->type == KeyRelease)
		XGetInputFocus(root->sys->dpy, &focus, &revert);
	if (focus == PointerRoot)
		focus = xe->xkey.subwindow;
	if (focus == None)
		focus = xe->xkey.window;
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
			atoms[NET_STRUT], 0L, 4L, False, XA_CARDINAL,
			&ret_type, &ret_size, &ret_items, &bytes_left, &xdata);
	if (status != Success || ret_size != 32 || ret_items != 4)
		return 0;

	win->sys->strut.left   = ((int*)xdata)[0];
	win->sys->strut.right  = ((int*)xdata)[1];
	win->sys->strut.top    = ((int*)xdata)[2];
	win->sys->strut.bottom = ((int*)xdata)[3];
	struts = list_insert(struts, win);
	for (list_t *cur = screens; cur; cur = cur->next)
		strut_copy(cur->data, win, 1);
	return strut_copy(root, win, 1);
}

static int strut_del(win_t *root, win_t *win)
{
	list_t *lwin = list_find(struts, win);
	if (lwin)
		struts = list_remove(struts, lwin, 0);
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
static void process_event(int type, XEvent *xe, win_t *root)
{
	Display  *dpy = root->sys->dpy;
	win_t *win = NULL;
	//printf("event: %d\n", type);

	/* Common data for all these events ... */
	ptr_t ptr = {}; mod_t mod = {};
	if (type == KeyPress    || type == KeyRelease    ||
	    type == ButtonPress || type == ButtonRelease ||
	    type == MotionNotify) {
		Window xid = getfocus(root, xe);
		if (!(win = win_find(dpy,xid,0)))
			return;
		ptr = x2ptr(xe);
		mod = x2mod(xe->xkey.state, type==KeyRelease||type==ButtonRelease);
	}

	/* Split based on event */
	if (type == KeyPress) {
		while (XCheckTypedEvent(dpy, KeyPress, xe));
		KeySym sym = XKeycodeToKeysym(dpy, xe->xkey.keycode, 0);
		printf("got xe %c %hhx\n", xk2ev(sym), mod2int(mod));
		wm_handle_event(win, xk2ev(sym), mod, ptr);
	}
	else if (type == KeyRelease) {
		//printf("release: %d\n", type);
	}
	else if (type == ButtonPress) {
		if (wm_handle_event(win, xb2ev(xe->xbutton.button), mod, ptr))
			XGrabPointer(dpy, xe->xbutton.root, True, PointerMotionMask|ButtonReleaseMask,
					GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
		else
			XAllowEvents(win->sys->dpy, ReplayPointer, CurrentTime);
	}
	else if (type == ButtonRelease) {
		XUngrabPointer(dpy, CurrentTime);
		wm_handle_event(win, xb2ev(xe->xbutton.button), mod, ptr);
	}
	else if (type == MotionNotify) {
		while (XCheckTypedEvent(dpy, MotionNotify, xe));
		wm_handle_ptr(win, ptr);
	}
	else if (type == EnterNotify || type == LeaveNotify) {
		printf("%s: %d\n", type==EnterNotify?"enter":"leave", type);
		event_t ev = EnterNotify ? EV_ENTER : EV_LEAVE;
		if ((win = win_find(dpy,xe->xcrossing.window,0)))
			wm_handle_event(win, ev, MOD(), PTR());
	}
	else if (type == FocusIn || type == FocusOut) {
		//printf("focus: %d\n", type);
		event_t ev = FocusIn ? EV_FOCUS : EV_UNFOCUS;
		if ((win = win_find(dpy,xe->xfocus.window,0)))
			wm_handle_event(win, ev, MOD(), PTR());
	}
	else if (type == ConfigureNotify) {
		printf("configure: %d\n", type);
	}
	else if (type == MapNotify) {
		printf("map: %d\n", type);
	}
	else if (type == UnmapNotify) {
		if ((win = win_find(dpy,xe->xunmap.window,0)) &&
		     win->sys->state != ST_HIDE) {
			if (!strut_del(root, win))
				wm_remove(win);
			else
				wm_update();
			win->sys->state = ST_HIDE;
		}
	}
	else if (type == DestroyNotify) {
		//printf("destroy: %d\n", type);
		if ((win = win_find(dpy,xe->xdestroywindow.window,0)))
			win_remove(win);
	}
	else if (type == ConfigureRequest) {
		XConfigureRequestEvent *cre = &xe->xconfigurerequest;
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
		if ((win = win_find(dpy,xe->xmaprequest.window,0)))
			sys_move(win, win->x, win->y, win->w, win->h);
	}
	else if (type == MapRequest) {
		printf("map_req: %d\n", type);
		if ((win = win_find(dpy,xe->xmaprequest.window,1))) {
			if (!strut_add(root, win))
				wm_insert(win);
			else
				wm_update();
		}
		XMapWindow(dpy,xe->xmaprequest.window);
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
	int b = 2*border;
	win->x = x; win->y = y;
	win->w = MAX(w,1+b); win->h = MAX(h,1+b);
	w      = MAX(w-b,1); h      = MAX(h-b,1);
	XMoveResizeWindow(win->sys->dpy, win->sys->xid, x, y, w, h);

	/* Flush events, so moving window doesn't cause re-focus
	 * There's probably a better way to do this */
	XEvent xe;
	XSync(win->sys->dpy, False);
	while (XCheckMaskEvent(win->sys->dpy, EnterWindowMask|LeaveWindowMask, &xe))
		printf("Skipping enter/leave event\n");
}

void sys_raise(win_t *win)
{
	//printf("sys_raise: %p\n", win);
	XRaiseWindow(win->sys->dpy, win->sys->xid);
	for (list_t *cur = struts; cur; cur = cur->next)
		XRaiseWindow(((win_t*)cur->data)->sys->dpy,
		             ((win_t*)cur->data)->sys->xid);
}

void sys_focus(win_t *win)
{
	//printf("sys_focus: %p\n", win);

	/* Set border on focused window */
	static win_t *last = NULL;
	if (last)
		XSetWindowBorder(last->sys->dpy, last->sys->xid, colors[CLR_UNFOCUS]);
	XSetWindowBorder(win->sys->dpy, win->sys->xid, colors[CLR_FOCUS]);
	XSetWindowBorderWidth(win->sys->dpy, win->sys->xid, border);
	last = win;

	/* Set actual focus */
	XSetInputFocus(win->sys->dpy, win->sys->xid,
			RevertToPointerRoot, CurrentTime);
	XSendEvent(win->sys->dpy, win->sys->xid, False, NoEventMask, &(XEvent){
		.type                 = ClientMessage,
		.xclient.window       = win->sys->xid,
		.xclient.message_type = atoms[WM_PROTO],
		.xclient.format       = 32,
		.xclient.data.l[0]    = atoms[WM_FOCUS],
		.xclient.data.l[1]    = CurrentTime,
	});
}

void sys_show(win_t *win, state_t state)
{
	win->sys->state = state;
	switch (state) {
	case ST_SHOW:
		printf("sys_show: show\n");
		XMapWindow(win->sys->dpy, win->sys->xid);
		XSync(win->sys->dpy, False);
		return;
	case ST_FULL:
		printf("sys_show: full\n");
		XMapWindow(win->sys->dpy, win->sys->xid);
		return;
	case ST_SHADE:
		printf("sys_show: shade\n");
		XMapWindow(win->sys->dpy, win->sys->xid);
		return;
	case ST_ICON:
		printf("sys_show: icon\n");
		return;
	case ST_HIDE:
		printf("sys_show: hide\n");
		XUnmapWindow(win->sys->dpy, win->sys->xid);
		return;
	case ST_CLOSE:
		printf("sys_show: close\n");
		XDestroyWindow(win->sys->dpy, win->sys->xid);
		return;
	}
}

void sys_watch(win_t *win, event_t ev, mod_t mod)
{
	//printf("sys_watch: %p - %x %hhx\n", win, ev, mod);
	XWindowAttributes attr;
	XGetWindowAttributes(win->sys->dpy, win->sys->xid, &attr);
	long mask = attr.your_event_mask;
	if (EV_MOUSE0 <= ev && ev <= EV_MOUSE7)
		XGrabButton(win->sys->dpy, ev2xb(ev), mod2x(mod), win->sys->xid, False,
				mod.up ? ButtonReleaseMask : ButtonPressMask,
				GrabModeSync, GrabModeAsync, None, None);
	else if (ev == EV_ENTER)
		XSelectInput(win->sys->dpy, win->sys->xid, EnterWindowMask|mask);
	else if (ev == EV_LEAVE)
		XSelectInput(win->sys->dpy, win->sys->xid, LeaveWindowMask|mask);
	else if (ev == EV_FOCUS || ev == EV_UNFOCUS)
		XSelectInput(win->sys->dpy, win->sys->xid, FocusChangeMask|mask);
	else
		XGrabKey(win->sys->dpy, XKeysymToKeycode(win->sys->dpy, ev2xk(ev)),
				mod2x(mod), win->sys->xid, True, GrabModeAsync, GrabModeAsync);
}

void sys_unwatch(win_t *win, event_t ev, mod_t mod)
{
	if (EV_MOUSE0 <= ev && ev <= EV_MOUSE7)
		XUngrabButton(win->sys->dpy, ev2xb(ev), mod2x(mod), win->sys->xid);
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
	border     = conf_get_int("main.border",     border);
	no_capture = conf_get_int("main.no-capture", no_capture);

	/* Open the display */
	if (!(dpy = XOpenDisplay(NULL)))
		error("Unable to get display");
	if (!(xid = DefaultRootWindow(dpy)))
		error("Unable to get root window");

	/* Setup X11 data */
	atoms[WM_PROTO]  = XInternAtom(dpy, "WM_PROTOCOLS",  False);
	atoms[WM_FOCUS]  = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	atoms[NET_STRUT] = XInternAtom(dpy, "_NET_WM_STRUT", False);

	colors[CLR_FOCUS]   = get_color(dpy, "#a0a0ff");
	colors[CLR_UNFOCUS] = get_color(dpy, "#101066");
	colors[CLR_URGENT]  = get_color(dpy, "#ff0000");
	printf("colors = #%06lx #%06lx #%06lx\n", colors[0], colors[1], colors[2]);

	/* Select window management events */
	XSelectInput(dpy, xid, SubstructureRedirectMask|SubstructureNotifyMask);
	xerrorxlib = XSetErrorHandler(xerror);

	return win_find(dpy, xid, 1);
}

void sys_run(win_t *root)
{
	/* Add each initial window */
	if (!no_capture) {
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
		XEvent xe;
		XNextEvent(root->sys->dpy, &xe);
		process_event(xe.type, &xe, root);
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
