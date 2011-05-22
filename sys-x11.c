#include <stdio.h>
#include <stdlib.h>
#include <search.h>

#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/keysym.h>

#include "util.h"
#include "sys.h"
#include "wm.h"

/* Internal structures */
struct win_sys {
	Window   xid;
	Display *dpy;
};

typedef struct {
	Key_t key;
	int   sym;
} keymap_t;

typedef enum {
	wm_proto, wm_focus, natoms
} atom_t;

Atom atoms[natoms];

/* Global data */
void *win_cache = NULL;

/* Conversion functions */
keymap_t key2sym[] = {
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
mod_t x2mod(unsigned int state, int up)
{
	return (mod_t){
	       .alt   = !!(state & Mod1Mask   ),
	       .ctrl  = !!(state & ControlMask),
	       .shift = !!(state & ShiftMask  ),
	       .win   = !!(state & Mod4Mask   ),
	       .up    = up,
	};
}

unsigned int mod2x(mod_t mod)
{
	return (mod.alt   ? Mod1Mask    : 0)
	     | (mod.ctrl  ? ControlMask : 0)
	     | (mod.shift ? ShiftMask   : 0)
	     | (mod.win   ? Mod4Mask    : 0);
}

/* - Keycodes */
Key_t x2key(KeySym sym)
{
	keymap_t *km = map_getr(key2sym,sym);
	return km ? km->key : sym;
}

KeySym key2x(Key_t key)
{
	keymap_t *km = map_get(key2sym,key);
	return km ? km->sym : key;
}

Key_t x2btn(int btn)
{
	return btn + key_mouse0;
}

int btn2x(Key_t key)
{
	return key - key_mouse0;
}

/* - Pointers */
ptr_t x2ptr(XEvent *_ev)
{
	XKeyEvent *ev = &_ev->xkey;
	return (ptr_t){ev->x, ev->y, ev->x_root, ev->y_root};
}
Window getfocus(win_t *root, XEvent *event)
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

/* Helper functions */
win_t *win_new(Display *dpy, Window xid)
{
	XWindowAttributes attr;
	XGetWindowAttributes(dpy, xid, &attr);
	win_t *win    = new0(win_t);
	win->x        = attr.x;
	win->y        = attr.y;
	win->w        = attr.width;
	win->h        = attr.height;
	win->sys      = new0(win_sys_t);
	win->sys->dpy = dpy;
	win->sys->xid = xid;
	printf("win_new: %p = %p, %d\n", win, dpy, (int)xid);
	return win;
}

int win_cmp(const void *_a, const void *_b)
{
	const win_t *a = _a, *b = _b;
	if (a->sys->dpy < b->sys->dpy) return -1;
	if (a->sys->dpy > b->sys->dpy) return  1;
	if (a->sys->xid < b->sys->xid) return -1;
	if (a->sys->xid > b->sys->xid) return  1;
	return 0;
}

win_t *win_find(Display *dpy, Window xid)
{
	if (!dpy || !xid)
		return NULL;
	//printf("win_find: %p, %d\n", dpy, (int)xid);
	win_sys_t sys = {.dpy=dpy, .xid=xid};
	win_t     tmp = {.sys=&sys};
	return *(win_t**)(tfind(&tmp, &win_cache, win_cmp) ?:
		tsearch(win_new(dpy,xid), &win_cache, win_cmp));
}

void win_remove(win_t *win)
{
	tdelete(win, &win_cache, win_cmp);
	free(win->sys);
	free(win);
}

/* Callbacks */
void process_event(int type, XEvent *ev, win_t *root)
{
	Display  *dpy = root->sys->dpy;

	/* Common data for all these events ... */
	win_t *win = NULL; ptr_t ptr; mod_t mod;
	if (type == KeyPress    || type == KeyRelease    ||
	    type == ButtonPress || type == ButtonRelease ||
	    type == MotionNotify) {
		Window xid = getfocus(root, ev);
		win = win_find(dpy,xid);
		ptr = x2ptr(ev);
		mod = x2mod(ev->xkey.state, type==KeyRelease||type==ButtonRelease);
	}

	/* Split based on event */
	//printf("event: %d\n", type);
	if (type == KeyPress) {
		while (XCheckTypedEvent(dpy, KeyPress, ev));
		KeySym sym = XKeycodeToKeysym(dpy, ev->xkey.keycode, 0);
		wm_handle_key(win, x2key(sym), mod, ptr);
	}
	else if (type == KeyRelease) {
		//printf("release: %d\n", type);
	}
	else if (type == ButtonPress) {
		wm_handle_key(win, x2btn(ev->xbutton.button), mod, ptr);
		XGrabPointer(dpy, win->sys->xid, True, PointerMotionMask|ButtonReleaseMask,
				GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
	}
	else if (type == ButtonRelease) {
		XUngrabPointer(dpy, CurrentTime);
		wm_handle_key(win, x2btn(ev->xbutton.button), mod, ptr);
	}
	else if (type == MotionNotify) {
		while (XCheckTypedEvent(dpy, MotionNotify, ev));
		wm_handle_ptr(win, ptr);
	}
	else if (type == ConfigureNotify) {
		//printf("configure: %d\n", type);
	}
	else if (type == MapNotify) {
		//printf("map: %d\n", type);
	}
	else if (type == DestroyNotify) {
		//printf("destory: %d\n", type);
		win_remove(win_find(dpy,ev->xdestroywindow.window));
	}
	else if (type == UnmapNotify) {
		//printf("unmap: %d\n", type);
		wm_remove(win_find(dpy,ev->xmap.window));
	}
	else if (type == ConfigureRequest) {
		XConfigureRequestEvent *cre = &ev->xconfigurerequest;
		XWindowChanges wc = {
			.x     = cre->x,     .y      = cre->y,
			.width = cre->width, .height = cre->height,
		};
		printf("configure_req: %d - %x, (0x%lx) %dx%d @ %d,%d\n",
				type, (int)cre->window, cre->value_mask,
				cre->height, cre->width, cre->x, cre->y);
		XConfigureWindow(dpy, cre->window, cre->value_mask, &wc);
	}
	else if (type == MapRequest) {
		printf("map_req: %d\n", type);
		wm_insert(win_find(dpy,ev->xmaprequest.window));
		XMapWindow(dpy,ev->xmaprequest.window);
	}
	else {
		printf("unknown event: %d\n", type);
	}
}

/*****************
 * Sys functions *
 *****************/
void sys_move(win_t *win, int x, int y, int w, int h)
{
	//printf("sys_move: %p - %d,%d  %dx%d\n", win, x, y, w, h);
	win->x = x; win->y = y;
	win->w = w; win->h = h;
	XMoveResizeWindow(win->sys->dpy, win->sys->xid, x, y, w, h);
}

void sys_raise(win_t *win)
{
	//printf("sys_raise: %p\n", win);
	XRaiseWindow(win->sys->dpy, win->sys->xid);
}

void sys_focus(win_t *win)
{
	//printf("sys_raise: %p\n", win);
	XEvent ev = {
		.type                 = ClientMessage,
		.xclient.window       = win->sys->xid,
		.xclient.message_type = atoms[wm_proto],
		.xclient.format       = 32,
		.xclient.data.l[0]    = atoms[wm_focus],
		.xclient.data.l[1]    = CurrentTime,
	};
	XSetInputFocus(win->sys->dpy, win->sys->xid,
			RevertToPointerRoot, CurrentTime);
	//XSetInputFocus(win->sys->dpy, PointerRoot,
	//		RevertToPointerRoot, CurrentTime);
	XSendEvent(win->sys->dpy, win->sys->xid, False, NoEventMask, &ev);
}

void sys_watch(win_t *win, Key_t key, mod_t mod)
{
	//printf("sys_watch: %p - %x %hhx\n", win, key, mod);
	if (key_mouse0 <= key && key <= key_mouse7)
		XGrabButton(win->sys->dpy, btn2x(key), mod2x(mod), win->sys->xid, True,
				mod.up ? ButtonReleaseMask : ButtonPressMask,
				GrabModeAsync, GrabModeAsync, None, None);
	else
		XGrabKey(win->sys->dpy, XKeysymToKeycode(win->sys->dpy, key2x(key)),
				mod2x(mod), win->sys->xid, True, GrabModeAsync, GrabModeAsync);
}

win_t *sys_init(void)
{
	Display *dpy = XOpenDisplay(NULL);
	Window   xid = DefaultRootWindow(dpy);
	atoms[wm_proto] = XInternAtom(dpy, "WM_PROTOCOLS",  False);
	atoms[wm_focus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	XSelectInput(dpy, xid, SubstructureNotifyMask|SubstructureRedirectMask);
	return win_find(dpy, xid);
}

void sys_run(win_t *root)
{
	/* Add each initial window */
	unsigned int nkids;
	Window par, win, *kids = NULL;
	if (XQueryTree(root->sys->dpy, root->sys->xid,
				&par, &win, &kids, &nkids))
		for(int i = 0; i < nkids; i++)
			wm_insert(win_find(root->sys->dpy, kids[i]));

	/* Main loop */
	for(;;)
	{
		XEvent ev;
		XNextEvent(root->sys->dpy, &ev);
		process_event(ev.type, &ev, root);
	}
}
