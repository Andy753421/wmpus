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

#include <stdio.h>
#include <stdlib.h>

#include "util.h"
#include "conf.h"
#include "sys.h"
#include "wm.h"

/* Configuration */
#ifndef MODKEY
#define MODKEY alt
#endif
static int MARGIN = 0;
static int STACK  = 25;

/* Enums */
typedef enum {
	none, move, resize
} drag_t;

typedef enum {
	split, stack, max, tab
} mode_t;

typedef enum {
	tiling, floating
} layer_t;

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
	win_t *win;      // the window
	int x, y, w, h;  // position of window (in this tag)
} flt_t;

typedef struct {
	list_t *cols;    // of col_t
	col_t  *col;     // focused col
	list_t *flts;    // of flt_t
	flt_t  *flt;     // focused flt
	layer_t layer;   // focused layer
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
#define FLT(node) ((flt_t*)(node)->data)
#define DPY(node) ((dpy_t*)(node)->data)
#define TAG(node) ((tag_t*)(node)->data)

#define tag_foreach_col(tag, dpy, col, row, win) \
	for (list_t *dpy =     tag ->dpys; dpy; dpy = dpy->next) \
	for (list_t *col = DPY(dpy)->cols; col; col = col->next) \
	for (list_t *row = COL(col)->rows; row; row = row->next) \
	for (win_t  *win = ROW(row)->win;  win; win = NULL)

#define tag_foreach_flt(tag, dpy, flt, win) \
	for (list_t *dpy =     tag ->dpys; dpy; dpy = dpy->next) \
	for (list_t *flt = DPY(dpy)->flts; flt; flt = flt->next) \
	for (win_t  *win = FLT(flt)->win;  win; win = NULL)      \

/* Window management data
 *   wm_* macros represent the currently focused item
 *   _only_ wm_focus protects against NULL pointers */
static wm_t *wm;
#define wm_tag   wm->tag
#define wm_dpy   wm->tag->dpy
#define wm_col   wm->tag->dpy->col
#define wm_row   wm->tag->dpy->col->row
#define wm_flt   wm->tag->dpy->flt
#define wm_focus get_focus()

/* Mouse drag data */
static drag_t  move_mode;
static list_t *move_lrow;
static list_t *move_lcol;
static list_t *move_lflt;
static ptr_t   move_prev;
static layer_t move_layer;
static struct { int v, h; } move_dir;

/********************
 * Helper functions *
 ********************/
static win_t *get_focus(void)
{
	if (!wm_tag || !wm_dpy)
		return NULL;
	switch (wm_dpy->layer) {
	case tiling:
		return wm_col && wm_row ? wm_row->win : NULL;
	case floating:
		return wm_flt ? wm_flt->win : NULL;
	}
	return NULL;
}

/* Search for the target window in a given tag
 * win may exist in other tags as well */
static int searchl(tag_t *tag, win_t *target,
		list_t **_dpy, list_t **_col, list_t **_row, list_t **_flt)
{
	tag_foreach_col(tag, dpy, col, row, win) {
		if (win == target) {
			if (_dpy) *_dpy = dpy;
			if (_col) *_col = col;
			if (_row) *_row = row;
			return tiling;
		}
	}
	tag_foreach_flt(tag, dpy, flt, win) {
		if (win == target) {
			if (_dpy) *_dpy = dpy;
			if (_flt) *_flt = flt;
			return floating;
		}
	}
	return -1;
}

static int search(tag_t *tag, win_t *target,
		dpy_t **_dpy, col_t **_col, row_t **_row, flt_t **_flt)
{
	list_t *dpy, *col, *row, *flt;
	switch (searchl(tag, target, &dpy, &col, &row, &flt)) {
	case tiling:
		if (_dpy) *_dpy = DPY(dpy);
		if (_col) *_col = COL(col);
		if (_row) *_row = ROW(row);
		return tiling;
	case floating:
		if (_dpy) *_dpy = DPY(dpy);
		if (_flt) *_flt = FLT(flt);
		return floating;
	}
	return -1;
}

/* Set the mode for the windows column in the current tag */
static void set_mode(win_t *win, mode_t mode)
{
	col_t *col;
	if (tiling != search(wm_tag, win, NULL, &col, NULL, NULL))
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

	dpy_t *dpy; col_t *col; row_t *row; flt_t *flt;
	switch (search(wm_tag, win, &dpy, &col, &row, &flt)) {
	case tiling:
		wm_dpy = dpy;
		wm_col = col;
		wm_row = row;
		dpy->layer = tiling;
		break;
	case floating:
		wm_dpy = dpy;
		wm_flt = flt;
		dpy->layer = floating;
		break;
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
		move_layer = searchl(wm_tag, win, NULL,
				&move_lcol, &move_lrow, &move_lflt);
		if (move_layer < 0)
			return;
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
		printf("tag:       <%-9p [%p->%p] >%-9p d=%-9p - %d\n",
				ltag->prev, ltag, ltag->data, ltag->next,
				tag->dpy, tag->name);
	for (list_t *ldpy = tag->dpys; ldpy; ldpy = ldpy->next) {
		dpy_t *dpy  = ldpy->data;
		win_t *geom = dpy->geom;
		printf("  dpy:     <%-9p [%p->%p] >%-9p %c=%-9p - %d,%d %dx%d\n",
				ldpy->prev, ldpy, ldpy->data, ldpy->next,
				dpy->layer == tiling   ? 'c' : 'f',
				dpy->layer == tiling   ? (void*)dpy->col : (void*)dpy->flt,
				geom->x, geom->y, geom->h, geom->w);
	for (list_t *lcol = dpy->cols; lcol; lcol = lcol->next) {
		col_t *col = lcol->data;
		printf("    col:   <%-9p [%p->%p] >%-9p r=%-9p - %dpx @ %d\n",
				lcol->prev, lcol, lcol->data, lcol->next,
				col->row, col->width, col->mode);
	for (list_t *lrow = col->rows; lrow; lrow = lrow->next) {
		row_t *row = lrow->data;
		win_t *win = row->win;
		printf("      win: <%-9p [%p>>%p] >%-9p focus=%d%d    - %4dpx \n",
				lrow->prev, lrow, win, lrow->next,
				col->row == row, wm_focus == win, win->h);
	} }
	for (list_t *lflt = dpy->flts; lflt; lflt = lflt->next) {
		flt_t *flt = lflt->data;
		win_t *win = flt->win;
		printf("    flt:   <%-9p [%p>>%p] >%-9p focus=%d%d    - %d,%d %dx%d \n",
				lflt->prev, lflt, win, lflt->next,
				dpy->flt == flt, wm_focus == flt->win,
				flt->x, flt->y, flt->h, flt->w);
	}  } }
}

/* Cleanly remove a window from a tag
 *   Determines the new focused row/col
 *   Prunes empty lists */
static layer_t cut_win(win_t *win, tag_t *tag)
{
	list_t *ldpy, *lcol, *lrow, *lflt;
	layer_t layer = searchl(tag, win, &ldpy, &lcol, &lrow, &lflt);

	if (layer == tiling) {
		dpy_t *dpy = DPY(ldpy);
		col_t *col = COL(lcol);
		col->row  = lrow->prev ? lrow->prev->data :
			    lrow->next ? lrow->next->data : NULL;
		col->rows = list_remove(col->rows, lrow, 1);
		if (col->rows == NULL && (lcol->next || lcol->prev)) {
			dpy->col  = lcol->prev ? lcol->prev->data :
				    lcol->next ? lcol->next->data : NULL;
			dpy->cols = list_remove(dpy->cols, lcol, 1);
		}
	}

	if (layer == floating) {
		dpy_t *dpy = DPY(ldpy);
		dpy->flts = list_remove(dpy->flts, lflt, 1);
		dpy->flt  = dpy->flts ? list_last(dpy->flts)->data : NULL;
		if (!dpy->flt && dpy->col && dpy->col->row)
			dpy->layer = tiling;
	}

	return layer;
}

/* Insert a window into the tiling layer
 *   The window is added immediately after the
 *   columns currently focused row */
static void put_win_col(win_t *win, tag_t *tag, dpy_t *dpy, col_t *col)
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
	tag->dpy->layer    = tiling;

	row->height = dpy->geom->h / MAX(nrows,1);
	if (nrows == 0) {
		int ncols = list_length(dpy->cols);
		col->width = dpy->geom->w / MAX(ncols-1,1);
	}
}

/* Insert a window into the floating layer */
static void put_win_flt(win_t *win, tag_t *tag, dpy_t *dpy)
{
	flt_t *flt = new0(flt_t);
	flt->win = win;
	flt->w   = dpy->geom->w / 2;
	flt->h   = dpy->geom->h / 2;
	flt->x   = dpy->geom->x + flt->w / 2;
	flt->y   = dpy->geom->y + flt->h / 2;
	if (dpy->flt) {
		flt->x = dpy->flt->x + 20;
		flt->y = dpy->flt->y + 20;
	}
	dpy->flts = list_append(dpy->flts, flt);
	tag->dpy        = dpy;
	tag->dpy->flt   = flt;
	tag->dpy->layer = floating;
}

/* Insert a window into a tag */
static void put_win(win_t *win, tag_t *tag, layer_t layer)
{
	if (layer == tiling)
		put_win_col(win, tag, tag->dpy, tag->dpy->col);
	if (layer == floating)
		put_win_flt(win, tag, tag->dpy);
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
	list_t *ldpy, *lcol, *lrow, *lflt;
	if (tiling != searchl(wm_tag, win, &ldpy, &lcol, &lrow, &lflt))
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
		cut_win(win, wm_tag);
		put_win_col(win, wm_tag, dpy, dst ? dst->data : NULL);
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
		if (tiling != searchl(wm_tag, wm_focus, &dpy, &col, &row, NULL))
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
			if (tiling != searchl(wm_tag, wm_focus, &dpy, &col, &row, NULL))
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

/* Raise the window in the floating */
static void raise_float(win_t *win)
{
	printf("raise_float: %p\n", win);
	list_t *cur;
	for (cur = wm_dpy->flts; cur; cur = cur->next)
		if (FLT(cur)->win == win)
			break;
	if (cur) {
		flt_t *flt = cur->data;
		wm_dpy->flts = list_remove(wm_dpy->flts, cur, 0);
		wm_dpy->flts = list_append(wm_dpy->flts, flt);
		sys_raise(win);
	}
}

/* Toggle between floating and tiling layers */
static void switch_layer(void)
{
	printf("switch_float: %p %d\n",
			wm_dpy, wm_dpy->layer);
	wm_dpy->layer = !wm_dpy->layer;
	wm_update();
}

/* Move current window between floating and tiling layers */
static void set_layer(win_t *win)
{
	if (!win) return;
	printf("set_float: %p %p\n", wm_dpy, win);
	wm_dpy->layer = !cut_win(win, wm_tag);
	put_win(win, wm_tag, wm_dpy->layer);
	wm_update();
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
	layer_t layer = cut_win(win, wm_tag);
	put_win(win, tag, layer);
	set_focus(wm_focus);
}

/* Switch to a different tag */
static void tag_switch(int name)
{
	printf("tag_switch: %d\n", name);
	tag_t *old = wm_tag;
	if ((wm_col == NULL || wm_row == NULL) && wm_flt == NULL) {
		while (old->dpys) {
			dpy_t *dpy = old->dpys->data;
			while (dpy->cols)
				dpy->cols = list_remove(dpy->cols, dpy->cols, 1);
			old->dpys = list_remove(old->dpys, old->dpys, 1);
		}
		list_t *ltag = list_find(wm->tags, old);
		wm->tags = list_remove(wm->tags, ltag, 1);
	}
	wm_tag = tag_find(name);
}

/* Tile all windows in the given display
 *   This performs all the actual window tiling
 *   Currently supports  split, stack and maximized modes */
static void wm_update_cols(dpy_t *dpy)
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
	win_t *focus = get_focus();
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
				break;
			}
			if (focus == win)
				sys_raise(win);
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
	tag_foreach_col(wm_tag, dpy, col, row, win)
		sys_show(win, st_show);
	tag_foreach_flt(wm_tag, dpy, flt, win)
		sys_show(win, st_show);
	for (list_t *tag = wm ->tags; tag; tag = tag->next)
		if (tag->data != wm_tag) {
			tag_foreach_col(TAG(tag), dpy, col, row, win)
					sys_show(win, st_hide);
			tag_foreach_flt(TAG(tag), dpy, flt, win)
					sys_show(win, st_hide);
		}

	/* Refresh the display */
	for (list_t *ldpy = wm_tag->dpys; ldpy; ldpy = ldpy->next)
		wm_update_cols(ldpy->data);
	tag_foreach_flt(wm_tag, ldpy, lflt, win) {
		flt_t *flt = lflt->data;
		sys_move(win, flt->x, flt->y, flt->w, flt->h);
		sys_raise(flt->win);
	}
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
	if (key == key_mouse1)
		raise_float(win);
	if (key_mouse0 <= key && key <= key_mouse7) {
		if (key == key_mouse1 && mod.MODKEY && !mod.up)
			return set_move(win,ptr,move),   1;
		if (key == key_mouse3 && mod.MODKEY && !mod.up)
			return set_move(win,ptr,resize), 1;
		if (move_mode != none && mod.up)
			return set_move(win,ptr,none),   1;
		if (key == key_mouse1 && !mod.up && win->h == STACK)
			return set_focus(win), wm_update(), 0;
		return 0;
	}

	/* Only handle key-down */
	if (mod.up)
		return 0;

	/* Misc */
	if (mod.MODKEY) {
#ifdef DEBUG
		if (key == key_f1) return raise_float(win), 1;
		if (key == key_f2) return set_focus(win), 1;
		if (key == key_f3) return sys_show(win, st_show), 1;
		if (key == key_f4) return sys_show(win, st_hide), 1;
#endif
		if (key == key_f5) return wm_update(),    1;
		if (key == key_f6) return print_txt(),    1;
		if (key == 'q')    return sys_exit(),     1;
	}

	/* Floating layer */
	if (key == ' ') {
		if (mod.MODKEY && mod.shift)
			return set_layer(win), 1;
		if (mod.MODKEY)
			return switch_layer(), 1;
	}

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
	if (key == key_enter && win->h != STACK)
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

	int dx = ptr.rx - move_prev.rx;
	int dy = ptr.ry - move_prev.ry;
	move_prev = ptr;

	if (move_layer == tiling && move_mode == resize) {
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

	if (move_layer == floating) {
		flt_t *flt = move_lflt->data;
		win_t *win = flt->win;
		if (move_mode == move)
			sys_move(win, win->x+dx, win->y+dy, win->w, win->h);
		else if (move_mode == resize)
			sys_move(win,
				win->x + dx * (move_dir.h < 0),
				win->y + dy * (move_dir.v < 0),
				win->w + dx *  move_dir.h,
				win->h + dy *  move_dir.v);
		flt->x = win->x; flt->y = win->y;
		flt->w = win->w; flt->h = win->h;
	}

	return 0;
}

void wm_insert(win_t *win)
{
	printf("wm_insert: %p\n", win);
	print_txt();

	/* Initialize window */
	win->wm = new0(win_wm_t);
	sys_watch(win, key_enter,  MOD());
	sys_watch(win, key_focus,  MOD());

	/* Add to screen */
	put_win(win, wm_tag, wm_dpy->layer);

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
		cut_win(win, tag->data);
	free(win->wm);
	set_focus(wm_focus);
	wm_update();
	print_txt();
}

void wm_init(win_t *root)
{
	printf("wm_init: %p\n", root);

	/* Load configuration */
	MARGIN = conf_get_int("main.margin", MARGIN);
	STACK  = conf_get_int("main.stack",  STACK);

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
	Key_t keys_s[] = {'h', 'j', 'k', 'l', 'q', ' ',
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
	Key_t keys_m[] = {'h', 'j', 'k', 'l', 'd', 's', 'm', 't', ' ',
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

void wm_free(win_t *root)
{
	/* Re-show and free all windows */
	while ( wm->tags) { tag_t *tag =  wm->tags->data;
	while (tag->dpys) { dpy_t *dpy = tag->dpys->data;
	while (dpy->cols) { col_t *col = dpy->cols->data;
	while (col->rows) { row_t *row = col->rows->data;
		sys_show(row->win, st_show);
		free(row->win->wm);
	col->rows = list_remove(col->rows, col->rows, 1); }
	dpy->cols = list_remove(dpy->cols, dpy->cols, 1); }
	while (dpy->flts) { flt_t *flt = dpy->flts->data;
		sys_show(flt->win, st_show);
		free(flt->win->wm);
	dpy->flts = list_remove(dpy->flts, dpy->flts, 1); }
	tag->dpys = list_remove(tag->dpys, tag->dpys, 1); }
	 wm->tags = list_remove( wm->tags,  wm->tags, 1); }

	/* Free remaining data */
	free(wm);
}
