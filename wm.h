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

/* Window manager interface:
 *
 * The window manager receives input events from the system
 * and responds by using the system to arrange the windows
 * according to it's layout.
 *
 * The window provided to these function is generally the
 * window with the keyboard or mouse focus. */

/* Called for each watched event */
int wm_handle_event(win_t *win, event_t ev, mod_t mod, ptr_t ptr);

/* Called for each mouse movement */
int wm_handle_ptr(win_t *win, ptr_t ptr);

/* Called when a window changes states */
int wm_handle_state(win_t *win, state_t prev, state_t next);

/* Begin managing a window, called for each new window */
void wm_insert(win_t *win);

/* Stop managing a window and free data */
void wm_remove(win_t *win);

/* First call, sets up key bindings, etc */
void wm_init(void);

/* First call, sets up key bindings, etc */
void wm_free(void);
