/*
 * Copyright (C) 2011 Andy Spencer <andy753421@gmail.com>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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


/* Basic window type */
typedef struct win_sys win_sys_t;
typedef struct win_wm  win_wm_t;
typedef struct {
	int x, y, z;
	int w, h;
	win_sys_t *sys;
	win_wm_t  *wm;
} win_t;

/* Generic key codes, also used for some other events
 * Keys map to their Unicode value */
typedef enum {
	key_alert       = '\a',
	key_backspace   = '\b',
	key_formfeed    = '\f',
	key_newline     = '\n',
	key_return      = '\r',
	key_tab         = '\t',
	key_vtab        = '\v',
	key_singlequote = '\'',
	key_doublequote = '\"',
	key_backslash   = '\\',
	key_question    = '\?',
	key_none        = 0xF0000, // unused Unicode space
	key_mouse0, key_mouse1, key_mouse2, key_mouse3,
	key_mouse4, key_mouse5, key_mouse6, key_mouse7,
	key_left, key_right, key_up,     key_down,
	key_home, key_end,   key_pageup, key_pagedown,
	key_f1, key_f2,  key_f3,  key_f4,
	key_f5, key_f6,  key_f7,  key_f8,
	key_f9, key_f10, key_f11, key_f12,
	key_alt, key_ctrl, key_shift, key_win,
	key_enter, key_leave, key_focus, key_unfocus,
} Key_t;

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

/* Window states */
typedef enum {
	st_show,  // show as regular window
	st_full,  // fullscreen/maximized
	st_shade, // show titlebar only
	st_icon,  // iconified/minimized
	st_hide,  // completely hidden
} state_t;


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

/* Start watching for a key events. The sys subsequently
 * calls wm_handle_key whenever the event occurs. */
void sys_watch(win_t *win, Key_t key, mod_t mod);

/* Stop watching a key event */
void sys_unwatch(win_t *win, Key_t key, mod_t mod);

/* Return a list of windows representing the geometry of the
 * physical displays attached to the computer. */
list_t *sys_info(win_t *root);

/* First call, calls wm_insert for each existing window */
win_t *sys_init(void);

/* Starts the main loop */
void sys_run(win_t *root);
