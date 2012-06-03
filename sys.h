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

/* Windowing system interface:
 *
 * The sys provides input to the window manager. It creates
 * the main loop and responds to input events from the user,
 * generally by converting them to a system independent form
 * and then passing them to the wm.
 *
 * The sys also provides the API used by the wm to position
 * and control windows. */


/* Window states */
typedef enum {
	ST_SHOW,  // show as regular window
	ST_FULL,  // fullscreen/maximized
	ST_SHADE, // show titlebar only
	ST_ICON,  // iconified/minimized
	ST_HIDE,  // completely hidden
	ST_CLOSE, // close the window
} state_t;

/* Basic window type */
typedef struct win_sys win_sys_t;
typedef struct win_wm  win_wm_t;
typedef struct {
	int x, y, z;
	int w, h;
	state_t    state;
	win_sys_t *sys;
	win_wm_t  *wm;
} win_t;

/* Generic key codes, also used for some other events
 * Keys map to their Unicode value */
typedef int event_t;
enum {
	EV_ALERT       = '\a',
	EV_BACKSPACE   = '\b',
	EV_FORMFEED    = '\f',
	EV_NEWLINE     = '\n',
	EV_RETURN      = '\r',
	EV_TAB         = '\t',
	EV_VTAB        = '\v',
	EV_SINGLEQUOTE = '\'',
	EV_DOUBLEQUOTE = '\"',
	EV_BACKSLASH   = '\\',
	EV_QUESTION    = '\?',
	EV_NONE        = 0xF0000, // unused Unicode space
	EV_MOUSE0, EV_MOUSE1, EV_MOUSE2, EV_MOUSE3,
	EV_MOUSE4, EV_MOUSE5, EV_MOUSE6, EV_MOUSE7,
	EV_LEFT, EV_RIGHT, EV_UP,     EV_DOWN,
	EV_HOME, EV_END,   EV_PAGEUP, EV_PAGEDOWN,
	EV_F1, EV_F2,  EV_F3,  EV_F4,
	EV_F5, EV_F6,  EV_F7,  EV_F8,
	EV_F9, EV_F10, EV_F11, EV_F12,
	EV_ALT, EV_CTRL, EV_SHIFT, EV_WIN,
	EV_ENTER, EV_LEAVE, EV_FOCUS, EV_UNFOCUS,
};

/* Key modifiers, up is for button release */
typedef struct {
	unsigned char alt   : 1;
	unsigned char ctrl  : 1;
	unsigned char shift : 1;
	unsigned char win   : 1;
	unsigned char up    : 1;
} mod_t;
#define MOD(...)     ((mod_t){__VA_ARGS__})
#define mod2int(mod) (*((unsigned char*)&(mod)))

/* Mouse movement */
typedef struct {
	int  x,  y;
	int rx, ry;
} ptr_t;
#define PTR(...) ((ptr_t){__VA_ARGS__})


/* Move the window to the specified location and set it's
 * geometry. The position and size include borders and
 * window decorations. */
void sys_move(win_t *win, int x, int y, int w, int h);

/* Rise the window above all other windows */
void sys_raise(win_t *win);

/* Give keyboard focus to the window and update window
 * decorations. */
void sys_focus(win_t *win);

/* Set the windows drawing state */
void sys_show(win_t *win, state_t st);

/* Start watching for an event. The sys subsequently
 * calls wm_handle_event whenever the event occurs. */
void sys_watch(win_t *win, event_t ev, mod_t mod);

/* Stop watching an event */
void sys_unwatch(win_t *win, event_t event, mod_t mod);

/* Return a list of windows representing the geometry of the
 * physical displays attached to the computer. */
list_t *sys_info(win_t *root);

/* First call, calls wm_insert for each existing window */
win_t *sys_init(void);

/* Starts the main loop */
void sys_run(win_t *root);

/* Exit main loop */
void sys_exit(void);

/* Free all static data, for memory debugging */
void sys_free(win_t *root);
