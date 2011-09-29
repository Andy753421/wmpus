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

/* Window manager interface:
 * 
 * The window manager receives input events from the system
 * and responds by using the system to arrange the windows
 * according to it's layout.
 *
 * The window provided to these function is generally the
 * window with the keyboard or mouse focus. */

/* Refresh the window layout */
void wm_update(void);

/* Called for each watched key press.
 * This is currently used for some other events such
 * as focus-in and focus-out as well. */
int wm_handle_key(win_t *win, Key_t key, mod_t mod, ptr_t ptr);

/* Called for each mouse movement */
int wm_handle_ptr(win_t *win, ptr_t ptr);

/* Begin managing a window, called for each new window */
void wm_insert(win_t *win);

/* Stop managing a window and free data */
void wm_remove(win_t *win);

/* First call, sets up key bindings, etc */
void wm_init(win_t *root);
