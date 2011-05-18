#include <stdio.h>
#include <stdlib.h>

#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/keysym.h>

#include "util.h"
#include "sys.h"
#include "wm.h"

/* Internal structures */
struct win_sys {
	Window   win;
	Display *dpy;
};

/* Conversion functions */
struct {
	Key_t key;
	int   sym;
} key2sym[] = {
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

mod_t x2mod(unsigned int state)
{
	return (mod_t){
	       .alt   = !!(state & Mod1Mask   ),
	       .ctrl  = !!(state & ControlMask),
	       .shift = !!(state & ShiftMask  ),
	       .win   = !!(state & Mod4Mask   ),
	};
}
unsigned int mod2x(mod_t mod)
{
	return mod.alt   ? Mod1Mask    : 0
	     | mod.ctrl  ? ControlMask : 0
	     | mod.shift ? ShiftMask   : 0
	     | mod.win   ? Mod4Mask    : 0;
}

KeySym key2x(Key_t key)
{
	return map_get(key2sym,key)->sym  ?: key;
}
Key_t x2key(KeySym sym)
{
	return map_getr(key2sym,sym)->key ?: sym;
}

int btn2x(Key_t key)
{
	return key - key_mouse0;
}
Key_t x2btn(int btn)
{
	return btn + key_mouse0;
}

ptr_t x2ptr(XEvent _ev)
{
	XKeyEvent ev = _ev.xkey;
	return (ptr_t){ev.x, ev.y, ev.x_root, ev.y_root};
}

/* Functions */
void sys_move(win_t *win, int x, int y, int w, int h)
{
	printf("sys_move: %p - %d,%d  %dx%d\n", win, x, y, w, h);
	XMoveResizeWindow(win->sys->dpy, win->sys->win, x, y, w, h);
}

void sys_raise(win_t *win)
{
	printf("sys_raise: %p\n", win);
	XRaiseWindow(win->sys->dpy, win->sys->win);
}

void sys_watch(win_t *win, Key_t key, mod_t mod)
{
	if (key_mouse0 <= key && key <= key_mouse7)
		XGrabButton(win->sys->dpy, btn2x(key), mod2x(mod), win->sys->win, True,
				mod.up ? ButtonReleaseMask : ButtonPressMask,
				GrabModeAsync, GrabModeAsync, None, None);
	else
		XGrabKey(win->sys->dpy, XKeysymToKeycode(win->sys->dpy, key2x(key)),
				mod2x(mod), win->sys->win, True, GrabModeAsync, GrabModeAsync);
}

win_t *win_new(Display *xdpy, Window xwin)
{
	if (!xdpy || !xwin)
		return NULL;
	XWindowAttributes attr;
	XGetWindowAttributes(xdpy, xwin, &attr);
	win_t *win    = new0(win_t);
	win->x        = attr.x;
	win->y        = attr.y;
	win->w        = attr.width;
	win->h        = attr.height;
	win->sys      = new0(win_sys_t);
	win->sys->dpy = xdpy;
	win->sys->win = xwin;
	return win;
}
void win_free(win_t *win)
{
	free(win->sys);
	free(win);
}

win_t *sys_init(void)
{
	Display *xdpy = XOpenDisplay(NULL);
	Window   xwin = DefaultRootWindow(xdpy);
	return win_new(xdpy, xwin);
}

void sys_run(win_t *root)
{
	Display *dpy = root->sys->dpy;
	for(;;)
	{
		XEvent ev;
		XNextEvent(dpy, &ev);
		//printf("event: %d\n", ev.type);
		if (ev.type == KeyPress && ev.xkey.subwindow) {
			while (XCheckTypedEvent(dpy, KeyPress, &ev));
			KeySym sym = XKeycodeToKeysym(dpy, ev.xkey.keycode, 0);
			wm_handle_key(win_new(dpy, ev.xkey.subwindow),
					x2key(sym), x2mod(ev.xkey.state), x2ptr(ev));
		}
		else if (ev.type == ButtonPress && ev.xbutton.subwindow) {
			wm_handle_key(win_new(dpy, ev.xkey.subwindow),
					x2btn(ev.xbutton.button), MOD(.up=0), x2ptr(ev));
			XGrabPointer(dpy, ev.xkey.subwindow, True,
					PointerMotionMask|ButtonReleaseMask, GrabModeAsync,
					GrabModeAsync, None, None, CurrentTime);
		}
		else if(ev.type == ButtonRelease) {
			XUngrabPointer(dpy, CurrentTime);
			wm_handle_key(win_new(dpy, ev.xkey.subwindow),
					x2btn(ev.xbutton.button), MOD(.up=1), x2ptr(ev));
		}
		else if(ev.type == MotionNotify) {
			while (XCheckTypedEvent(dpy, MotionNotify, &ev));
			wm_handle_ptr(win_new(dpy, ev.xkey.subwindow), x2ptr(ev));
		}
	}
}
