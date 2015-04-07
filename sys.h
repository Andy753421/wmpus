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

/* Windowing system interface:
 *
 * The sys provides input to the window manager. It creates
 * the main loop and responds to input events from the user,
 * generally by converting them to a system independent form
 * and then passing them to the wm.
 *
 * The sys also provides the API used by the wm to position
 * and control windows. */

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
list_t *sys_info(void);

/* First call, calls wm_insert for each existing window */
void sys_init(void);

/* Starts the main loop */
void sys_run(void);

/* Exit main loop */
void sys_exit(void);

/* Free all static data, for memory debugging */
void sys_free(void);
