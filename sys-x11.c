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
static int stack      = 25;

/* Internal structures */
struct win_sys {
	Window   xid;
	Display *dpy;
	struct {
		int left, right, top, bottom;
	} strut;
};

typedef struct {
	event_t ev;
	int     sym;
} event_map_t;

typedef enum {
	WM_PROTO, WM_FOCUS, WM_DELETE,
	NET_STATE, NET_FULL, NET_STRUT,
	NET_TYPE, NET_DIALOG,
	NATOMS
} atom_t;

typedef enum {
	CLR_FOCUS, CLR_UNFOCUS, CLR_URGENT, NCOLORS
} color_t;

/* Global data */
static win_t *root;
static int   running;
static void *cache;
static Atom atoms[NATOMS];
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned long colors[NCOLORS];
static list_t *screens;
static list_t *struts;

/* Debug functions */
static char *state_map[] = {
	[ST_HIDE ] "hide ",
	[ST_SHOW ] "show ",
	[ST_FULL ] "full ",
	[ST_MAX  ] "max  ",
	[ST_SHADE] "shade",
	[ST_ICON ] "icon ",
	[ST_CLOSE] "close",
};

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
	to->sys->strut.left   += scale*left;
	to->sys->strut.right  += scale*right;
	to->sys->strut.top    += scale*top;
	to->sys->strut.bottom += scale*bottom;
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

	win->sys->strut.left   = ((long*)xdata)[0];
	win->sys->strut.right  = ((long*)xdata)[1];
	win->sys->strut.top    = ((long*)xdata)[2];
	win->sys->strut.bottom = ((long*)xdata)[3];
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
static Atom win_prop(win_t *win, atom_t prop);
static win_t *win_find(Display *dpy, Window xid, int create);

static win_t *win_new(Display *dpy, Window xid)
{
	Window trans;
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

	if (root) {
		if (strut_add(root, win))
			win->type = TYPE_TOOLBAR;

		if (win_prop(win, NET_TYPE) == atoms[NET_DIALOG])
			win->type = TYPE_DIALOG;

		if (win_prop(win, NET_STATE) == atoms[NET_FULL])
			win->state = ST_FULL;

		if (XGetTransientForHint(dpy, xid, &trans))
			win->parent = win_find(dpy, trans, 0);

		XSelectInput(dpy, xid, PropertyChangeMask);
	}

	printf("win_new: win=%p x11=(%p,%d) state=%x pos=(%d,%d %dx%d) type=%s\n",
			win, dpy, (int)xid, win->state,
			win->x, win->y, win->w, win->h,
			win->type == TYPE_NORMAL  ? "normal"  :
			win->type == TYPE_DIALOG  ? "dialog"  :
			win->type == TYPE_TOOLBAR ? "toolbar" : "unknown");

	if (root)
		wm_insert(win);

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
	if (win != root) {
		strut_del(root, win);
		wm_remove(win);
	}
	tdelete(win, &cache, win_cmp);
	win_free(win);
}

static int win_viewable(Display *dpy, Window xid)
{
	XWindowAttributes attr;
	if (XGetWindowAttributes(dpy, xid, &attr))
		return attr.map_state == IsViewable;
	else
		return True;
}

static int win_msg(win_t *win, atom_t msg)
{
	int n, found = 0;
	Atom *protos;
	if (!XGetWMProtocols(win->sys->dpy, win->sys->xid, &protos, &n))
		return 0;

	while (!found && n--)
		found = protos[n] == atoms[msg];
	XFree(protos);
	if (!found)
		return 0;

	XSendEvent(win->sys->dpy, win->sys->xid, False, NoEventMask, &(XEvent){
		.xclient.type         = ClientMessage,
		.xclient.window       = win->sys->xid,
		.xclient.message_type = atoms[WM_PROTO],
		.xclient.format       = 32,
		.xclient.data.l[0]    = atoms[msg],
		.xclient.data.l[1]    = CurrentTime,
	});
	return 1;
}

static Atom win_prop(win_t *win, atom_t prop)
{
	int format;
	unsigned long nitems, bytes;
	unsigned char *buf = NULL;
	Atom atom, type = XA_ATOM;
	if (XGetWindowProperty(win->sys->dpy, win->sys->xid, atoms[prop],
			0L, sizeof(Atom), False, type, &type, &format, &nitems, &bytes, &buf) || !buf)
		return 0;
	atom = *(Atom *)buf;
	XFree(buf);
	return atom;
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
		//printf("button-press %p\n", win);
		ptr = x2ptr(xe);
		mod = x2mod(xe->xkey.state, type==KeyRelease||type==ButtonRelease);
	}

	/* Split based on event */
	if (type == KeyPress) {
		while (XCheckTypedEvent(dpy, KeyPress, xe));
		KeySym sym = XLookupKeysym(&xe->xkey, 0);
		//printf("got xe %c %hhx\n", xk2ev(sym), mod2int(mod));
		wm_handle_event(win, xk2ev(sym), mod, ptr);
	}
	else if (type == KeyRelease) {
		//printf("release: %lx\n", xe->xkey.window);
	}
	else if (type == ButtonPress) {
		if (wm_handle_event(win, xb2ev(xe->xbutton.button), mod, ptr)) {
			//printf("grab pointer\n");
			XGrabPointer(dpy, xe->xbutton.root, True, PointerMotionMask|ButtonReleaseMask,
					GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
		} else {
			//printf("allow events\n");
			XAllowEvents(win->sys->dpy, ReplayPointer, xe->xbutton.time);
		}
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
		printf("%s: %lx\n", type==EnterNotify?"enter":"leave",
				xe->xcrossing.window);
		event_t ev = type == EnterNotify ? EV_ENTER : EV_LEAVE;
		if ((win = win_find(dpy,xe->xcrossing.window,0)))
			wm_handle_event(win, ev, MOD(), PTR());
	}
	else if (type == FocusIn || type == FocusOut) {
		//printf("focus: %lx\n", xe->xfocus.window);
		event_t ev = type == FocusIn ? EV_FOCUS : EV_UNFOCUS;
		if ((win = win_find(dpy,xe->xfocus.window,0)))
			wm_handle_event(win, ev, MOD(), PTR());
	}
	else if (type == ConfigureNotify) {
		//printf("configure: %lx\n", xe->xconfigure.window);
	}
	else if (type == MapNotify) {
		printf("map: %lx\n", xe->xmap.window);
	}
	else if (type == UnmapNotify) {
		if ((win = win_find(dpy,xe->xunmap.window,0)) &&
		     win->state != ST_HIDE) {
			printf("unmap: %lx\n", xe->xunmap.window);
			wm_handle_state(win, win->state, ST_HIDE);
			win->state = ST_HIDE;
		}
	}
	else if (type == DestroyNotify) {
		printf("destroy: %lx\n", xe->xdestroywindow.window);
		if ((win = win_find(dpy,xe->xdestroywindow.window,0)))
			win_remove(win);
	}
	else if (type == ConfigureRequest) {
		XConfigureRequestEvent *cre = &xe->xconfigurerequest;
		printf("configure_req: %lx - (0x%lx) %dx%d @ %d,%d\n",
				cre->window, cre->value_mask,
				cre->height, cre->width, cre->x, cre->y);
		if ((win = win_find(dpy,cre->window,1))) {
			XSendEvent(dpy, cre->window, False, StructureNotifyMask, &(XEvent){
				.xconfigure.type              = ConfigureNotify,
				.xconfigure.display           = win->sys->dpy,
				.xconfigure.event             = win->sys->xid,
				.xconfigure.window            = win->sys->xid,
				.xconfigure.x                 = win->x,
				.xconfigure.y                 = win->y,
				.xconfigure.width             = win->w,
				.xconfigure.height            = win->h,
				.xconfigure.border_width      = border,
			});
			XSync(win->sys->dpy, False);
		}
	}
	else if (type == MapRequest) {
		printf("map_req: %lx\n", xe->xmaprequest.window);
		win = win_find(dpy,xe->xmaprequest.window,1);
		// fixme, for hide -> max, etc
		if (win->state == ST_HIDE) {
			wm_handle_state(win, win->state, ST_SHOW);
			win->state = ST_SHOW;
		}
		sys_show(win, win->state);
	}
	else if (type == ClientMessage) {
		XClientMessageEvent *cme = &xe->xclient;
		printf("client_msg: %lx - %ld %ld,%ld,%ld,%ld,%ld\n",
				cme->window, cme->message_type,
				cme->data.l[0], cme->data.l[1], cme->data.l[2],
				cme->data.l[3], cme->data.l[4]);
		if ((win = win_find(dpy,cme->window,0))     &&
		    (cme->message_type == atoms[NET_STATE]) &&
		    (cme->data.l[1] == atoms[NET_FULL] ||
		     cme->data.l[2] == atoms[NET_FULL])) {
			state_t next = (cme->data.l[0] == 1 || /* _NET_WM_STATE_ADD    */
			               (cme->data.l[0] == 2 && /* _NET_WM_STATE_TOGGLE */
			                win->state != ST_FULL)) ? ST_FULL : ST_SHOW;
			wm_handle_state(win, win->state, next);
			sys_show(win, next);
		}
	}
	else if (type == PropertyNotify) {
		printf("prop: %lx - %d\n", xe->xproperty.window, xe->xproperty.state);
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
	if (err->request_code == X_ChangeWindowAttributes && err->error_code == BadAccess)
		error("Another window manager is already running");
	return xerrorxlib(dpy, err);
}

static int xnoerror(Display *dpy, XErrorEvent *err)
{
	return 0;
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
	XConfigureWindow(win->sys->dpy, win->sys->xid, CWX|CWY|CWWidth|CWHeight,
		&(XWindowChanges) { .x=x, .y=y, .width=w, .height=h });
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

	/* Set actual focus */
	XSetInputFocus(win->sys->dpy, win->sys->xid,
			RevertToPointerRoot, CurrentTime);
	//win_msg(win, WM_FOCUS);

	/* Set border on focused window */
	static win_t *last = NULL;
	if (last)
		XSetWindowBorder(last->sys->dpy, last->sys->xid, colors[CLR_UNFOCUS]);
	XSync(win->sys->dpy, False);
	XSetWindowBorder(win->sys->dpy, win->sys->xid, colors[CLR_FOCUS]);
	last = win;
}

void sys_show(win_t *win, state_t state)
{
	//if (win->state == state)
	//	return;

	/* Debug */
	printf("sys_show: %p: %s -> %s\n", win,
			state_map[win->state], state_map[state]);

	/* Find screen */
	win_t *screen = NULL;
	if (state == ST_FULL || state == ST_MAX) {
		for (list_t *cur = screens; cur; cur = cur->next) {
			screen = cur->data;
			if (win->x >= screen->x && win->x <= screen->x+screen->w &&
			    win->y >= screen->y && win->y <= screen->y+screen->h)
				break;
		}
	}

	/* Update properties */
	if (state == ST_FULL)
		XChangeProperty(win->sys->dpy, win->sys->xid, atoms[NET_STATE], XA_ATOM, 32,
		                PropModeReplace, (unsigned char*)&atoms[NET_FULL], 1);
	else if (state != ST_FULL)
		XChangeProperty(win->sys->dpy, win->sys->xid, atoms[NET_STATE], XA_ATOM, 32,
				PropModeReplace, (unsigned char*)0, 0);

	/* Update border */
	if (state == ST_SHOW || state == ST_MAX || state == ST_SHADE)
		XSetWindowBorderWidth(win->sys->dpy, win->sys->xid, border);
	else if (state == ST_FULL)
		XSetWindowBorderWidth(win->sys->dpy, win->sys->xid, 0);

	/* Map/Unmap window */
	if (state == ST_SHOW || state == ST_FULL || state == ST_MAX || state == ST_SHADE)
		XMapWindow(win->sys->dpy, win->sys->xid);
	else if (state == ST_HIDE)
		XUnmapWindow(win->sys->dpy, win->sys->xid);

	/* Resize windows */
	if (state == ST_SHOW) {
		sys_move(win, win->x, win->y, win->w, win->h);
	} else if (state == ST_MAX) {
		sys_move(win, screen->x, screen->y, screen->w, screen->h);
	} else if (state == ST_FULL) {
		XWindowChanges wc = {
			.x      = screen->x - screen->sys->strut.left ,
			.y      = screen->y - screen->sys->strut.top ,
			.width  = screen->w + screen->sys->strut.left + screen->sys->strut.right,
			.height = screen->h + screen->sys->strut.top  + screen->sys->strut.bottom
		};
		win->x = wc.x;     win->y = wc.y;
		win->w = wc.width; win->h = wc.height;
		XConfigureWindow(win->sys->dpy, win->sys->xid, CWX|CWY|CWWidth|CWHeight, &wc);
		XMoveResizeWindow(win->sys->dpy, win->sys->xid, wc.x, wc.y, wc.width, wc.height);
	} else if (state == ST_SHADE) {
		XConfigureWindow(win->sys->dpy, win->sys->xid, CWHeight,
			&(XWindowChanges) { .height = stack });
	}

	/* Raise window */
	if (state == ST_FULL || state == ST_MAX)
		XRaiseWindow(win->sys->dpy, win->sys->xid);

	/* Close windows */
	if (state == ST_CLOSE) {
		if (!win_msg(win, WM_DELETE)) {
			XGrabServer(win->sys->dpy);
			XSetErrorHandler(xnoerror);
			XSetCloseDownMode(win->sys->dpy, DestroyAll);
			XKillClient(win->sys->dpy, win->sys->xid);
			XSync(win->sys->dpy, False);
			XSetErrorHandler(xerror);
			XUngrabServer(win->sys->dpy);
		}
	}

	/* Update state */
	win->state = state;
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
			screen->sys = new0(win_sys_t);
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
	stack      = conf_get_int("main.stack",      stack);
	border     = conf_get_int("main.border",     border);
	no_capture = conf_get_int("main.no-capture", no_capture);

	/* Open the display */
	if (!(dpy = XOpenDisplay(NULL)))
		error("Unable to get display");
	if (!(xid = DefaultRootWindow(dpy)))
		error("Unable to get root window");

	/* Setup X11 data */
	atoms[WM_PROTO]    = XInternAtom(dpy, "WM_PROTOCOLS",               False);
	atoms[WM_FOCUS]    = XInternAtom(dpy, "WM_TAKE_FOCUS",              False);
	atoms[WM_DELETE]   = XInternAtom(dpy, "WM_DELETE_WINDOW",           False);
	atoms[NET_STATE]   = XInternAtom(dpy, "_NET_WM_STATE",              False);
	atoms[NET_FULL]    = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN",   False);
	atoms[NET_STRUT]   = XInternAtom(dpy, "_NET_WM_STRUT",              False);
	atoms[NET_TYPE]    = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE",        False);
	atoms[NET_DIALOG]  = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);

	colors[CLR_FOCUS]   = get_color(dpy, "#a0a0ff");
	colors[CLR_UNFOCUS] = get_color(dpy, "#101066");
	colors[CLR_URGENT]  = get_color(dpy, "#ff0000");
	//printf("colors = #%06lx #%06lx #%06lx\n", colors[0], colors[1], colors[2]);

	/* Select window management events */
	XSelectInput(dpy, xid, SubstructureRedirectMask|SubstructureNotifyMask);
	xerrorxlib = XSetErrorHandler(xerror);

	return root = win_find(dpy, xid, 1);
}

void sys_run(win_t *root)
{
	/* Add each initial window */
	if (!no_capture) {
		unsigned int nkids;
		Window par, xid, *kids = NULL;
		if (XQueryTree(root->sys->dpy, root->sys->xid,
					&par, &xid, &kids, &nkids)) {
			for(int i = 0; i < nkids; i++)
				if (win_viewable(root->sys->dpy, kids[i]))
					win_find(root->sys->dpy, kids[i], 1);
			XFree(kids);
		}
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
	while (screens) {
		win_free(screens->data);
		screens = list_remove(screens, screens, 0);
	}
	tdestroy(cache, (void(*)(void*))win_free);
}
