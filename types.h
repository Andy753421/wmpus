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

/* Forward definitions for sys and wm */
typedef struct win_sys win_sys_t;
typedef struct win_wm  win_wm_t;

/* Window states */
typedef enum {
	ST_HIDE,  // completely hidden
	ST_SHOW,  // show as regular window
	ST_FULL,  // fullscreen (without decorations)
	ST_MAX,   // maximized (with decorations)
	ST_SHADE, // show titlebar only
	ST_ICON,  // iconified/minimized
	ST_CLOSE, // close the window
} state_t;

/* Window types */
typedef enum {
	TYPE_NORMAL,
	TYPE_DIALOG,
	TYPE_TOOLBAR,
	TYPE_CURSOR,
} type_t;

/* Basic window type */
typedef struct win {
	int x, y, z;
	int w, h;
	state_t state;
	type_t  type;
	struct win *parent;
	win_sys_t  *sys;
	win_wm_t   *wm;
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

/* Mouse movement */
typedef struct {
	int  x,  y;
	int rx, ry;
} ptr_t;

/* Creation functions */
#define MOD(...) ((mod_t){__VA_ARGS__})
#define PTR(...) ((ptr_t){__VA_ARGS__})

/* Conversion functions */
#define mod2int(mod) (*((unsigned char*)&(mod)))
