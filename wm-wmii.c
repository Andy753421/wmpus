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

#include <stdio.h>
#include <stdlib.h>

#include "util.h"
#include "sys.h"
#include "wm.h"

#ifndef MODKEY
#define MODKEY alt
#endif
#ifndef MARGIN
#define MARGIN 0
#endif
#ifndef STACK
#define STACK  25
#endif

/* Enums */
typedef enum {
	none, move, resize
} drag_t;

typedef enum {
	split, stack, max, tab
} mode_t;

/* Window structure types */
struct win_wm { };

typedef struct {
	win_t  *win;     // the window
	int     height;  // win height in _this_ tag
} row_t;

typedef struct {
	list_t *rows;    // of row_t
	row_t  *row;     // focused row
	int     width;   // column width
	mode_t  mode;    // display mode
} col_t;

typedef struct {
	list_t *cols;    // of col_t
	col_t  *col;     // focused col
	win_t  *geom;    // display size and position
} dpy_t;

typedef struct {
	list_t *dpys;    // of dpy_t
	dpy_t  *dpy;     // focused dpy
	int     name;    // tag name
} tag_t;

typedef struct {
	list_t *tags;    // of tag_t
	tag_t  *tag;     // focused tag
	win_t  *root;    // root/background window
	list_t *screens; // display geometry
} wm_t;

#define WIN(node) ((win_t*)(node)->data)
#define ROW(node) ((row_t*)(node)->data)
#define COL(node) ((col_t*)(node)->data)
#define DPY(node) ((dpy_t*)(node)->data)
#define TAG(node) ((tag_t*)(node)->data)

#define tag_foreach(tag, dpy, col, row, win) \
	for (list_t *dpy =     tag ->dpys; dpy; dpy = dpy->next) \
	for (list_t *col = DPY(dpy)->cols; col; col = col->next) \
	for (list_t *row = COL(col)->rows; row; row = row->next) \
	for (win_t  *win = ROW(row)->win;  win; win = NULL)      \

/* Window management data
 *   wm_* macros represent the currently focused item
 *   _only_ wm_focus protects against NULL pointers */
static wm_t *wm;
#define wm_win   wm->tag->dpy->col->row->win
#define wm_row   wm->tag->dpy->col->row
#define wm_col   wm->tag->dpy->col
#define wm_dpy   wm->tag->dpy
#define wm_tag   wm->tag
#define wm_focus (wm_tag && wm_dpy && wm_col && wm_row ? wm_win : NULL)

/* Mouse drag data */
static drag_t  move_mode;
static list_t *move_lrow;
static list_t *move_lcol;
static ptr_t   move_prev;
static struct { int v, h; } move_dir;

/********************
 * Helper functions *
 ********************/
/* Search for the target window in a given tag
 * win may exist in other tags as well */
static int searchl(tag_t *tag, win_t *target,
		list_t **_dpy, list_t **_col, list_t **_row)
{
	tag_foreach(tag, dpy, col, row, win) {
		if (win == target) {
			if (_dpy) *_dpy = dpy;
			if (_col) *_col = col;
			if (_row) *_row = row;
			return 1;
		}
	}
	return 0;
}

static int search(tag_t *tag, win_t *target,
		dpy_t **_dpy, col_t **_col, row_t **_row)
{
	list_t *dpy, *col, *row;
	if (searchl(tag, target, &dpy, &col, &row)) {
		if (_dpy) *_dpy = DPY(dpy);
		if (_col) *_col = COL(col);
		if (_row) *_row = ROW(row);
		return 1;
	}
	return 0;
}

/* Set the mode for the windows column in the current tag */
static void set_mode(win_t *win, mode_t mode)
{
	col_t *col;
	if (!search(wm_tag, win, NULL, &col, NULL))
		return;
	printf("set_mode: %p, %d -> %d\n",
			col, col->mode, mode);
	col->mode = mode;
	if (col->mode == split)
		for (list_t *cur = col->rows; cur; cur = cur->next) {
			row_t *row = cur->data;
			row->height = wm_dpy->geom->h;
		}
	wm_update();
}

/* Focus the window in the current tag and record
 * it as the currently focused window */
static void set_focus(win_t *win)
{
	if (win == NULL || win == wm->root) {
		sys_focus(wm->root);
		return;
	}

	/* - Only grab mouse button on unfocused window,
	 *   this prevents stealing all mouse clicks from client windows,
	 * - A better way may be to re-send mouse clicks to client windows
	 *   using the return value from wm_handle_key */
	for (int i = key_mouse1; i < key_mouse7; i++) {
		if (wm_focus)
			sys_watch(wm_focus, i, MOD());
		sys_unwatch(win, i, MOD());
	}

	dpy_t *dpy; col_t *col; row_t *row;
	if (search(wm_tag, win, &dpy, &col, &row)) {
		wm_dpy = dpy;
		wm_col = col;
		wm_row = row;
	}
	sys_focus(win);
}

/* Save mouse start location when moving/resizing windows */
static void set_move(win_t *win, ptr_t ptr, drag_t drag)
{
	printf("set_move: %d - %p@%d,%d\n",
			drag, win, ptr.rx, ptr.ry);
	move_mode = drag;
	if (drag == move || drag == resize) {
		searchl(wm_tag, win, NULL, &move_lcol, &move_lrow);
		move_prev = ptr;
		int midy = win->y + (win->h/2);
		int midx = win->x + (win->w/2);
		move_dir.v = ptr.ry < midy ? -1 : +1;
		move_dir.h = ptr.rx < midx ? -1 : +1;
	}
}

/* Print a text representation of the window layout
 * Quite useful for debugging */
static void print_txt(void)
{
	for (list_t *ltag = wm->tags; ltag; ltag = ltag->next) {
		tag_t *tag = ltag->data;
		printf("tag:       <%-9p [%p->%p] >%-9p !%-9p -  %d\n",
				ltag->prev, ltag, ltag->data, ltag->next,
				tag->dpy, tag->name);
	for (list_t *ldpy = tag->dpys; ldpy; ldpy = ldpy->next) {
		dpy_t *dpy  = ldpy->data;
		win_t *geom = dpy->geom;
		printf("  dpy:     <%-9p [%p->%p] >%-9p !%-9p -  %d,%d %dx%d\n",
				ldpy->prev, ldpy, ldpy->data, ldpy->next,
				dpy->col, geom->x, geom->y, geom->h, geom->w);
	for (list_t *lcol = dpy->cols; lcol; lcol = lcol->next) {
		col_t *col = lcol->data;
		printf("    col:   <%-9p [%p->%p] >%-9p !%-9p -  %dpx @ %d\n",
				lcol->prev, lcol, lcol->data, lcol->next,
				col->row, col->width, col->mode);
	for (list_t *lrow = col->rows; lrow; lrow = lrow->next) {
		row_t *row = lrow->data;
		win_t *win = row->win;
		printf("      win: <%-9p [%p>>%p] >%-9p !%-9p -  %4dpx focus=%d%d\n",
				lrow->prev, lrow, win, lrow->next,
				win, win->h, col->row == row, wm_focus == win);
	} } } }
}

/* Cleanly remove a window from a tag
 *   Determines the new focused row/col
 *   Prunes empty lists */
static void cut_win(tag_t *tag, win_t *win)
{
	list_t *ldpy, *lcol, *lrow;
	if (!searchl(tag, win, &ldpy, &lcol, &lrow))
		return;
	col_t  *col  = COL(lcol);
	dpy_t  *dpy  = DPY(ldpy);

	col->row  = lrow->prev ? lrow->prev->data :
	            lrow->next ? lrow->next->data : NULL;
	col->rows = list_remove(col->rows, lrow);

	if (col->rows == NULL && (lcol->next || lcol->prev)) {
		dpy->col  = lcol->prev ? lcol->prev->data :
			    lcol->next ? lcol->next->data : NULL;
		dpy->cols = list_remove(dpy->cols, lcol);
	}
}

/* Insert a window at a given location
 *   The window is added immediately after the
 *   columns currently focused row */
static void put_win(win_t *win, tag_t *tag, dpy_t *dpy, col_t *col)
{
	row_t *row = new0(row_t);
	row->win = win;

	if (col == NULL) {
		col = new0(col_t);
		dpy->cols = list_insert(dpy->cols, col);
	}

	int nrows = list_length(col->rows);
	if (col->row) {
		list_t *prev = list_find(col->rows, col->row);
		list_insert_after(prev, row);
	} else {
		col->rows = list_insert(col->rows, row);
	}
	tag->dpy           = dpy;
	tag->dpy->col      = col;
	tag->dpy->col->row = row;

	row->height = dpy->geom->h / MAX(nrows,1);
	if (nrows == 0) {
		int ncols = list_length(dpy->cols);
		col->width = dpy->geom->w / MAX(ncols-1,1);
	}
}

/* Move a window up, down, left, or right
 *   This handles moving with a column, between
 *   columns, and between multiple monitors. */
static void shift_window(win_t *win, int col, int row)
{
	if (!win) return;
	printf("shift_window: %p - %+d,%+d\n", win, col, row);
	print_txt();
	printf("shift_window: >>>\n");
	list_t *ldpy, *lcol, *lrow;
	if (!searchl(wm_tag, win, &ldpy, &lcol, &lrow))
		return;
	dpy_t *dpy = ldpy->data;
	if (row != 0) {
		/* Move with a column, just swap rows */
		list_t *src = lrow, *dst = NULL;
		if (row < 0) dst = src->prev;
		if (row > 0) dst = src->next;
		if (src && dst) {
			printf("swap: %p <-> %p\n", src->data, dst->data);
			row_t *tmp = src->data;
			src->data = dst->data;
			dst->data = tmp;
			goto update;
		}
	} else {
		/* Moving between columns */
		int onlyrow = !lrow->prev && !lrow->next;
		list_t *src = lcol, *dst = NULL;
		if (col < 0) {
			if (src->prev) {
				/* Normal move between columns */
				dst = src->prev;
			} else if (!onlyrow) {
				/* Create new column */
				dpy->cols = list_insert(dpy->cols, new0(col_t));
				dst = src->prev;
			} else if (ldpy->prev) {
				/* Move to next monitor */
				dpy = ldpy->prev->data;
				dst = list_last(dpy->cols);
			} else {
				/* We, shall, not, be,
				 * we shall not be moved */
				return;
			}
		}
		if (col > 0) {
			if (src->next) {
				dst = src->next;
			} else if (!onlyrow) {
				dpy->cols = list_append(dpy->cols, new0(col_t));
				dst = src->next;
			} else if (ldpy->next) {
				dpy = ldpy->next->data;
				dst = dpy->cols;
			} else {
				return;
			}
		}
		cut_win(wm_tag, win);
		put_win(win, wm_tag, dpy, dst ? dst->data : NULL);
		goto update;
	}
update:
	print_txt();
	wm_update();
}

/* Get next/prev item, with wraparound */
static list_t *get_next(list_t *list, int forward)
{
	list_t *next = forward ? list->next : list->prev;
	if (next == NULL) {
		next = list;
		while ((list = forward ? next->prev : next->next))
			next = list;
	}
	return next;
}

/* Move keyboard focus in a given direction */
static void shift_focus(int cols, int rows)
{
	printf("shift_focus: %+d,%+d\n", cols, rows);
	if (rows != 0 && wm_focus) {
		/* Move focus up/down */
		list_t *dpy, *col, *row;
		if (!searchl(wm_tag, wm_focus, &dpy, &col, &row))
			return;
		row_t *next = get_next(row, rows > 0)->data;
		set_focus(next->win);
		if (COL(col)->mode != split)
			wm_update();
	}
	if (cols != 0) {
		/* Move focus left/right */
		list_t *dpy, *col, *row, *ndpy, *ncol = NULL;
		if (wm_focus) {
			/* Currently focused on a window */
			if (!searchl(wm_tag, wm_focus, &dpy, &col, &row))
				return;
			ncol = cols > 0 ? col->next : col->prev;
		} else {
			/* Currently focused on an empty display */
			dpy = list_find(wm_tag->dpys, wm_dpy);
		}
		if (ncol == NULL) {
			/* Moving focus to a different display */
			ndpy = get_next(dpy, cols > 0);
			ncol = cols > 0 ? DPY(ndpy)->cols :
				list_last(DPY(ndpy)->cols);
			wm_dpy = ndpy->data;
		}
		if (ncol && COL(ncol) && COL(ncol)->row)
			set_focus(COL(ncol)->row->win);
		else
			sys_focus(wm->root);
	}
}

/* Allocate a new tag */
static tag_t *tag_new(list_t *screens, int name)
{
	tag_t *tag = new0(tag_t);
	tag->name  = name;
	for (list_t *cur = screens; cur; cur = cur->next) {
		dpy_t *dpy  = new0(dpy_t);
		dpy->geom = cur->data;
		tag->dpys = list_append(tag->dpys, dpy);
	}
	tag->dpy  = tag->dpys->data;
	return tag;
}

/* Search for a tag
 *   If it does not exist it is based on the
 *   display geometry in wm->screens */
static tag_t *tag_find(int name)
{
	tag_t *tag = NULL;
	for (list_t *cur = wm->tags; cur; cur = cur->next)
		if (name == TAG(cur)->name) {
			tag = cur->data;
			break;
		}
	if (!tag) {
		tag = tag_new(wm->screens, name);
		wm->tags = list_append(wm->tags, tag);
	}
	return tag;
}

/* Move the window from the current tag to the new tag
 *   Unlike wmii, only remove the current tag, not all tags */
static void tag_set(win_t *win, int name)
{
	printf("tag_set: %p %d\n", win, name);
	if (wm_tag->name == name)
		return;
	tag_t *tag = tag_find(name);
	cut_win(wm_tag, win);
	put_win(win, tag, tag->dpy, tag->dpy->col);
	set_focus(wm_focus);
}

/* Switch to a different tag */
static void tag_switch(int name)
{
	printf("tag_switch: %d\n", name);
	if (wm_col == NULL || wm_row == NULL)
		wm->tags = list_remove(wm->tags,
				list_find(wm->tags, wm_tag));
	wm_tag = tag_find(name);
}

/* Tile all windows in the given display
 *   This performs all the actual window tiling
 *   Currently supports  split, stack and maximized modes */
static void wm_update_dpy(dpy_t *dpy)
{
	int  x=0,  y=0; // Current window top-left position
	int tx=0, ty=0; // Total size (sum of initial col widths and row heights w/o margin)
	int mx=0, my=0; // Maximum usable size (screen size minus margins)
	int       sy=0; // Stack size (height of focused stack window)

	/* Scale horizontally */
	x  = dpy->geom->x;
	mx = dpy->geom->w - (list_length(dpy->cols)+1)*MARGIN;
	for (list_t *lcol = dpy->cols; lcol; lcol = lcol->next)
		tx += COL(lcol)->width;
	for (list_t *lcol = dpy->cols; lcol; lcol = lcol->next)
		COL(lcol)->width *= (float)mx / tx;

	/* Scale each column vertically */
	for (list_t *lcol = dpy->cols; lcol; lcol = lcol->next) {
		col_t *col = lcol->data;
		int nrows = list_length(col->rows);
		ty = 0;
		for (list_t *lrow = col->rows; lrow; lrow = lrow->next)
			ty += ROW(lrow)->height;
		y  = dpy->geom->y;
		my = dpy->geom->h - (MARGIN + (nrows-1)* MARGIN    + MARGIN);
		sy = dpy->geom->h - (MARGIN + (nrows-1)*(MARGIN/2) + MARGIN)
		                  -           (nrows-1)* STACK;
		for (list_t *lrow = col->rows; lrow; lrow = lrow->next) {
			win_t *win = ROW(lrow)->win;
			win->h = ROW(lrow)->height;
			int height = 0;
			switch (col->mode) {
			case split:
				sys_move(win, x+MARGIN, y+MARGIN,
					col->width, win->h * ((float)my / ty));
				height = win->h;
				y += height + MARGIN;
				break;
			case stack:
				if (lrow->next && ROW(lrow->next)->win == col->row->win) {
					/* Hack to prevent flashing */
					win_t *next = ROW(lrow->next)->win;
					sys_move(next, x+MARGIN, y+MARGIN+STACK+MARGIN/2,
						col->width, sy);
				}
				height = win == col->row->win ? sy : STACK;
				sys_move(win, x+MARGIN, y+MARGIN,
					col->width, height);
				y += height + (MARGIN/2);
				break;
			case max:
			case tab:
				sys_move(win, x+MARGIN, 0+MARGIN,
					col->width, dpy->geom->h-2*MARGIN);
				if (col->row->win == win)
					sys_raise(win);
				break;
			}
			ROW(lrow)->height = win->h;
		}
		x += col->width + MARGIN;
	}
}

/*******************************
 * Window management functions *
 *******************************/
void wm_update(void)
{
	/* Show/hide tags */
	tag_foreach(wm_tag, dpy, col, row, win)
		sys_show(win, st_show);
	for (list_t *tag = wm ->tags; tag; tag = tag->next)
		tag_foreach(TAG(tag), dpy, col, row, win)
			if (tag->data != wm_tag)
				sys_show(win, st_hide);

	/* Refresh the display */
	for (list_t *ldpy = wm_tag->dpys; ldpy; ldpy = ldpy->next)
		wm_update_dpy(ldpy->data);
	if (wm_focus)
		set_focus(wm_focus);
}

int wm_handle_key(win_t *win, Key_t key, mod_t mod, ptr_t ptr)
{
	if (!win || win == wm_dpy->geom) return 0;
	//printf("wm_handle_key: %p - %x %c%c%c%c%c\n", win, key,
	//	mod.up    ? '^' : 'v',
	//	mod.alt   ? 'a' : '-',
	//	mod.ctrl  ? 'c' : '-',
	//	mod.shift ? 's' : '-',
	//	mod.win   ? 'w' : '-');

	/* Mouse movement */
	if (key_mouse0 <= key && key <= key_mouse7) {
		if (key == key_mouse1 && mod.MODKEY && !mod.up)
			return set_move(win,ptr,move),   1;
		if (key == key_mouse3 && mod.MODKEY && !mod.up)
			return set_move(win,ptr,resize), 1;
		if (move_mode != none && mod.up)
			return set_move(win,ptr,none),   1;
		if (key == key_mouse1 && !mod.up && win->h == STACK)
			return wm_update(),              0;
		return 0;
	}

	/* Only handle key-down */
	if (mod.up)
		return 0;

	/* Misc */
	if (mod.MODKEY) {
#ifdef DEBUG
		if (key == key_f1) return sys_raise(win), 1;
		if (key == key_f2) return set_focus(win), 1;
		if (key == key_f3) return sys_show(win, st_show), 1;
		if (key == key_f4) return sys_show(win, st_hide), 1;
#endif
		if (key == key_f5) return wm_update(),    1;
		if (key == key_f6) return print_txt(),    1;
	}
	if (key_mouse0 <= key && key <= key_mouse7)
		sys_raise(win);

	/* Movement commands */
	if (mod.MODKEY && mod.shift) {
		switch (key) {
		case 'h': return shift_window(wm_focus,-1, 0), 1;
		case 'j': return shift_window(wm_focus, 0,+1), 1;
		case 'k': return shift_window(wm_focus, 0,-1), 1;
		case 'l': return shift_window(wm_focus,+1, 0), 1;
		default: break;
		}
	}
	else if (mod.MODKEY) {
		switch (key) {
		case 'h': return shift_focus(-1, 0), 1;
		case 'j': return shift_focus( 0,+1), 1;
		case 'k': return shift_focus( 0,-1), 1;
		case 'l': return shift_focus(+1, 0), 1;
		default: break;
		}
	}

	/* Column mode commands */
	if (mod.MODKEY) {
		switch (key) {
		case 'd': return set_mode(win, split), 1;
		case 's': return set_mode(win, stack), 1;
		case 'm': return set_mode(win, max),   1;
		case 't': return set_mode(win, tab),   1;
		default: break;
		}
	}

	/* Tag switching */
	if (mod.MODKEY && '0' <= key && key <= '9') {
		int name = key - '0';
		if (mod.shift)
			tag_set(win, name);
		else
			tag_switch(name);
		wm_update();
	}

	/* Focus change */
	if (key == key_enter)
		return set_focus(win), 1;

	if (key_mouse0 <= key && key <= key_mouse7)
		return set_focus(win), 0;

	/* Reset focus after after focus change,
	 * not sure what is causing the focus change in the first place
	 * but preventing that would be a better solution */
	if (key == key_focus)
		sys_focus(wm_focus ?: wm->root);

	return 0;
}

int wm_handle_ptr(win_t *cwin, ptr_t ptr)
{
	//printf("wm_handle_ptr: %p - %d,%d %d,%d (%d) -- \n",
	//		cwin, ptr.x, ptr.y, ptr.rx, ptr.ry, move_mode);

	if (move_mode == none)
		return 0;

	/* Tiling */
	int dx = ptr.rx - move_prev.rx;
	int dy = ptr.ry - move_prev.ry;
	move_prev = ptr;
	if (move_mode == resize) {
		list_t *vert = move_dir.v < 0 ? move_lrow->prev : move_lrow->next;
		list_t *horz = move_dir.h < 0 ? move_lcol->prev : move_lcol->next;
		if (vert) {
			ROW(move_lrow)->height += move_dir.v * dy;
			ROW(vert)->height      -= move_dir.v * dy;
		}
		if (horz) {
			COL(move_lcol)->width  += move_dir.h * dx;
			COL(horz)->width       -= move_dir.h * dx;
		}
		wm_update();
	}

	/* Floating */
	//win_t *mwin = move_win;
	//int dx = ptr.rx - move_prev.rx;
	//int dy = ptr.ry - move_prev.ry;
	//move_prev = ptr;
	//if (move_mode == move)
	//	sys_move(mwin, mwin->x+dx, mwin->y+dy, mwin->w, mwin->h);
	//else if (move_mode == resize)
	//	sys_move(mwin, mwin->x, mwin->y, mwin->w+dx, mwin->h+dy);

	return 0;
}

void wm_insert(win_t *win)
{
	printf("wm_insert: %p\n", win);
	print_txt();

	/* Initialize window */
	win->wm = new0(win_wm_t);
	sys_watch(win, key_enter, MOD());
	sys_watch(win, key_focus, MOD());

	/* Add to screen */
	put_win(win, wm_tag, wm_dpy, wm_col);

	/* Arrange */
	wm_update();
	set_focus(wm_focus);
	print_txt();
}

void wm_remove(win_t *win)
{
	printf("wm_remove: %p\n", win);
	print_txt();
	for (list_t *tag = wm->tags; tag; tag = tag->next)
		cut_win(tag->data, win);
	set_focus(wm_focus);
	wm_update();
	print_txt();
}
void wm_init(win_t *root)
{
	printf("wm_init: %p\n", root);

	/* Hack, fix screen order */
	list_t *screens = sys_info(root);
	list_t *left  = screens;
	list_t *right = screens->next;
	if (left && right && WIN(left)->x > WIN(right)->x) {
	    	void *tmp   = left->data;
	    	left->data  = right->data;
	    	right->data = tmp;
	}

	wm          = new0(wm_t);
	wm->root    = root;
	wm->screens = screens;
	wm->tag     = tag_new(wm->screens, 1);
	wm->tags    = list_insert(NULL, wm->tag);

	Key_t keys_e[] = {key_enter, key_focus};
	Key_t keys_s[] = {'h', 'j', 'k', 'l',
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
	Key_t keys_m[] = {'h', 'j', 'k', 'l', 'd', 's', 'm', 't',
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
		key_f1, key_f2, key_f3, key_f4, key_f5, key_f6,
		key_mouse1, key_mouse3};
	for (int i = 0; i < countof(keys_e); i++)
		sys_watch(root, keys_e[i],  MOD());
	for (int i = 0; i < countof(keys_m); i++)
		sys_watch(root, keys_m[i], MOD(.MODKEY=1));
	for (int i = 0; i < countof(keys_s); i++)
		sys_watch(root, keys_s[i], MOD(.MODKEY=1,.shift=1));
}
