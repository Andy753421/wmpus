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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "conf.h"
#include "types.h"
#include "sys.h"
#include "wm.h"

/* Configuration */
#ifndef MODKEY
#define MODKEY alt
#endif
static int margin = 0;
static int stack  = 25;

/* Enums */
typedef enum {
	NONE, MOVE, RESIZE
} drag_t;

typedef enum {
	SPLIT, STACK, FULL, TAB
} layout_t;

typedef enum {
	TILING, FLOATING
} layer_t;

/* Window structure types */
struct win_wm { };

typedef struct {
	win_t   *win;      // the window
	int      height;   // win height in _this_ tag
	state_t  state;    // state of window
} row_t;

typedef struct {
	list_t  *rows;     // of row_t
	row_t   *row;      // focused row
	int      width;    // column width
	layout_t layout;   // column layout
} col_t;

typedef struct {
	win_t   *win;      // the window
	int x, y, w, h;    // position of window (in this tag)
	state_t  state;    // state of window
} flt_t;

typedef struct {
	list_t  *cols;     // of col_t
	col_t   *col;      // focused col
	list_t  *flts;     // of flt_t
	flt_t   *flt;      // focused flt
	layer_t  layer;    // focused layer
	win_t   *geom;     // display size and position
} dpy_t;

typedef struct {
	list_t  *dpys;     // of dpy_t
	dpy_t   *dpy;      // focused dpy
	char     name[64]; // tag name
} tag_t;

typedef struct {
	list_t  *tags;     // of tag_t
	tag_t   *tag;      // focused tag
	list_t  *screens;  // display geometry
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

/* Prototypes */
void wm_update(void);

/********************
 * Helper functions *
 ********************/
static int sort_win(void *a, void *b)
{
	return ((win_t*)a)->x > ((win_t*)b)->x ?  1 :
	       ((win_t*)a)->x < ((win_t*)b)->x ? -1 : 0;
}

static win_t *get_focus(void)
{
	if (!wm_tag || !wm_dpy)
		return NULL;
	switch (wm_dpy->layer) {
	case TILING:
		return wm_col && wm_row ? wm_row->win : NULL;
	case FLOATING:
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
			return TILING;
		}
	}
	tag_foreach_flt(tag, dpy, flt, win) {
		if (win == target) {
			if (_dpy) *_dpy = dpy;
			if (_flt) *_flt = flt;
			return FLOATING;
		}
	}
	return -1;
}

static int search(tag_t *tag, win_t *target,
		dpy_t **_dpy, col_t **_col, row_t **_row, flt_t **_flt)
{
	list_t *dpy, *col, *row, *flt;
	switch (searchl(tag, target, &dpy, &col, &row, &flt)) {
	case TILING:
		if (_dpy) *_dpy = DPY(dpy);
		if (_col) *_col = COL(col);
		if (_row) *_row = ROW(row);
		return TILING;
	case FLOATING:
		if (_dpy) *_dpy = DPY(dpy);
		if (_flt) *_flt = FLT(flt);
		return FLOATING;
	}
	return -1;
}

/* Set the layout for the windows column in the current tag */
static void set_mode(win_t *win, layout_t layout)
{
	col_t *col;
	if (TILING != search(wm_tag, win, NULL, &col, NULL, NULL))
		return;
	printf("set_mode: %p, %d -> %d\n",
			col, col->layout, layout);
	col->layout = layout;
	if (col->layout == SPLIT)
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
	if (win == NULL) {
		sys_focus(NULL);
		return;
	}

	dpy_t *dpy; col_t *col; row_t *row; flt_t *flt;
	switch (search(wm_tag, win, &dpy, &col, &row, &flt)) {
	case TILING:
		wm_dpy = dpy;
		wm_col = col;
		wm_row = row;
		dpy->layer = TILING;
		break;
	case FLOATING:
		wm_dpy = dpy;
		wm_flt = flt;
		dpy->layer = FLOATING;
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
	if (drag == MOVE || drag == RESIZE) {
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
		printf("tag:       <%-9p [%p->%p] >%-9p d=%-9p - %s\n",
				ltag->prev, ltag, ltag->data, ltag->next,
				tag->dpy, tag->name);
	for (list_t *ldpy = tag->dpys; ldpy; ldpy = ldpy->next) {
		dpy_t *dpy  = ldpy->data;
		win_t *geom = dpy->geom;
		printf("  dpy:     <%-9p [%p->%p] >%-9p %c=%-9p - %d,%d %dx%d\n",
				ldpy->prev, ldpy, ldpy->data, ldpy->next,
				dpy->layer == TILING ? 'c' : 'f',
				dpy->layer == TILING ? (void*)dpy->col : (void*)dpy->flt,
				geom->x, geom->y, geom->h, geom->w);
	for (list_t *lcol = dpy->cols; lcol; lcol = lcol->next) {
		col_t *col = lcol->data;
		printf("    col:   <%-9p [%p->%p] >%-9p r=%-9p - %dpx @ %d\n",
				lcol->prev, lcol, lcol->data, lcol->next,
				col->row, col->width, col->layout);
	for (list_t *lrow = col->rows; lrow; lrow = lrow->next) {
		row_t *row = lrow->data;
		win_t *win = row->win;
		printf("      row: <%-9p [%p>>%p] >%-9p focus=%d%d    - %4dpx \n",
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

	if (layer == TILING) {
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

	if (layer == FLOATING) {
		dpy_t *dpy = DPY(ldpy);
		dpy->flts = list_remove(dpy->flts, lflt, 1);
		dpy->flt  = dpy->flts ? list_last(dpy->flts)->data : NULL;
		if (!dpy->flt && dpy->col && dpy->col->row)
			dpy->layer = TILING;
	}

	return layer;
}

/* Insert a window into the tiling layer
 *   The window is added immediately after the
 *   columns currently focused row */
static void put_win_col(win_t *win, tag_t *tag, dpy_t *dpy, col_t *col)
{
	row_t *row = new0(row_t);
	row->win   = win;
	row->state = win->state;

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
	tag->dpy->layer    = TILING;

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
	flt->win   = win;
	flt->w     = dpy->geom->w / 2;
	flt->h     = dpy->geom->h / 2;
	flt->x     = dpy->geom->x + flt->w / 2;
	flt->y     = dpy->geom->y + flt->h / 2;
	flt->state = win->state ?: ST_SHOW;
	if (dpy->flt) {
		flt->x = dpy->flt->x + 20;
		flt->y = dpy->flt->y + 20;
	}
	dpy->flts = list_append(dpy->flts, flt);
	tag->dpy        = dpy;
	tag->dpy->flt   = flt;
	tag->dpy->layer = FLOATING;
}

/* Insert a window into a tag */
static void put_win(win_t *win, tag_t *tag, layer_t layer)
{
	if (layer == TILING)
		put_win_col(win, tag, tag->dpy, tag->dpy->col);
	if (layer == FLOATING)
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
	if (TILING != searchl(wm_tag, win, &ldpy, &lcol, &lrow, &lflt))
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
		if (TILING != searchl(wm_tag, wm_focus, &dpy, &col, &row, NULL))
			return;
		row_t *next = get_next(row, rows > 0)->data;
		set_focus(next->win);
		if (COL(col)->layout != SPLIT)
			wm_update();
	}
	if (cols != 0) {
		/* Move focus left/right */
		list_t *dpy, *col, *row, *ndpy, *ncol = NULL;
		if (wm_focus) {
			/* Currently focused on a window */
			if (TILING != searchl(wm_tag, wm_focus, &dpy, &col, &row, NULL))
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
			sys_focus(NULL);
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
static tag_t *tag_new(list_t *screens, const char *name)
{
	tag_t *tag = new0(tag_t);
	strncpy(tag->name, name, sizeof(tag->name));
	for (list_t *cur = screens; cur; cur = cur->next) {
		dpy_t *dpy  = new0(dpy_t);
		dpy->geom = cur->data;
		tag->dpys = list_append(tag->dpys, dpy);
	}
	tag->dpy  = tag->dpys->data;
	for (list_t *dpy = tag->dpys; dpy; dpy = dpy->next)
		if (DPY(dpy)->geom->z > tag->dpy->geom->z)
			tag->dpy = dpy->data;
	return tag;
}

/* Search for a tag
 *   If it does not exist it is based on the
 *   display geometry in wm->screens */
static tag_t *tag_find(const char *name)
{
	tag_t *tag = NULL;
	for (list_t *cur = wm->tags; cur; cur = cur->next)
		if (!strcmp(name, TAG(cur)->name)) {
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
static void tag_set(win_t *win, const char *name)
{
	printf("tag_set: %p %s\n", win, name);
	if (!strcmp(wm_tag->name, name))
		return;
	tag_t *tag = tag_find(name);
	layer_t layer = cut_win(win, wm_tag);
	put_win(win, tag, layer);
	set_focus(wm_focus);
}

/* Switch to a different tag */
static void tag_switch(const char *name)
{
	printf("tag_switch: %s\n", name);
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

	float rx=0, ry=0; // Residuals for floating point round off

	/* Scale horizontally */
	x  = dpy->geom->x;
	mx = dpy->geom->w - (list_length(dpy->cols)+1)*margin;
	for (list_t *lcol = dpy->cols; lcol; lcol = lcol->next)
		tx += COL(lcol)->width;
	for (list_t *lcol = dpy->cols; lcol; lcol = lcol->next)
		COL(lcol)->width = residual(COL(lcol)->width * (float)mx/tx, &rx);

	/* Scale each column vertically */
	win_t *focus = get_focus();
	for (list_t *lcol = dpy->cols; lcol; lcol = lcol->next) {
		col_t *col = lcol->data;
		int nrows = list_length(col->rows);
		ty = 0;
		for (list_t *lrow = col->rows; lrow; lrow = lrow->next)
			ty += ROW(lrow)->height;
		y  = dpy->geom->y;
		my = dpy->geom->h - (margin + (nrows-1)* margin    + margin);
		sy = dpy->geom->h - (margin + (nrows-1)*(margin/2) + margin)
		                  -           (nrows-1)* stack;
		for (list_t *lrow = col->rows; lrow; lrow = lrow->next) {
			win_t *win = ROW(lrow)->win;
			if (ROW(lrow)->state != ST_SHOW) {
				sys_show(win, ROW(lrow)->state);
				continue;
			}
			win->h = ROW(lrow)->height;
			state_t state = ST_SHOW;
			switch (col->layout) {
			case SPLIT:
				sys_move(win, x+margin, y+margin, col->width,
					residual(win->h * ((float)my/ty), &ry));
				y += win->h + margin;
				break;
			case STACK:
				if (lrow->next && ROW(lrow->next)->win == col->row->win) {
					/* Hack to prevent flashing */
					win_t *next = ROW(lrow->next)->win;
					sys_move(next, x+margin, y+margin+stack+margin/2,
						col->width, sy);
					sys_show(next, ST_SHOW);
				}
				int isfocus = win == col->row->win;
				state  = isfocus ? ST_SHOW : ST_SHADE;
				sys_show(win, state);
				sys_move(win, x+margin, y+margin, col->width, sy);
				y += (isfocus ? sy : stack) + (margin/2);
				break;
			case FULL:
			case TAB:
				sys_move(win, x+margin, 0+margin,
					col->width, dpy->geom->h-2*margin);
				break;
			}
			sys_show(win, state);
			if (focus == win)
				sys_raise(win);
			ROW(lrow)->height = win->h;
		}
		x += col->width + margin;
	}
}

/* Refresh the window layout */
void wm_update(void)
{
	/* Updates window sizes */
	for (list_t *ldpy = wm_tag->dpys; ldpy; ldpy = ldpy->next)
		wm_update_cols(ldpy->data);
	tag_foreach_flt(wm_tag, ldpy, lflt, win) {
		flt_t *flt = lflt->data;
		sys_move(win, flt->x, flt->y, flt->w, flt->h);
		sys_raise(flt->win);
		sys_show(flt->win, flt->state);
	}

	/* Hide other tags */
	for (list_t *tag = wm ->tags; tag; tag = tag->next)
		if (tag->data != wm_tag) {
			tag_foreach_col(TAG(tag), dpy, col, row, win)
				sys_show(win, ST_HIDE);
			tag_foreach_flt(TAG(tag), dpy, flt, win)
				sys_show(win, ST_HIDE);
		}

	/* Set focused window */
	if (wm_focus)
		set_focus(wm_focus);
}

/*******************************
 * Window management functions *
 *******************************/
int wm_handle_event(win_t *win, event_t ev, mod_t mod, ptr_t ptr)
{
	//printf("wm_handle_event: %p - %x %c%c%c%c%c\n", win, ev,
	//	mod.up    ? '^' : 'v',
	//	mod.alt   ? 'a' : '-',
	//	mod.ctrl  ? 'c' : '-',
	//	mod.shift ? 's' : '-',
	//	mod.win   ? 'w' : '-');

	/* Mouse events */
	if (win && EV_MOUSE0 <= ev && ev <= EV_MOUSE7) {
		if (ev == EV_MOUSE1 && !mod.MODKEY && !mod.up)
			return raise_float(win),         0;
		if ((ev == EV_MOUSE3 && mod.MODKEY && !mod.up) ||
		    (ev == EV_MOUSE1 && mod.MODKEY && !mod.up && mod.shift))
			return set_move(win,ptr,RESIZE), 1;
		if (ev == EV_MOUSE1 && mod.MODKEY && !mod.up)
			return set_move(win,ptr,MOVE),   1;
		if (move_mode != NONE && mod.up)
			return set_move(win,ptr,NONE),   1;
		if (ev == EV_MOUSE1 && !mod.up && win->state == ST_SHADE)
			return set_focus(win), wm_update(), 0;
		return 0;
	}

	/* Only handle key-down */
	if (mod.up)
		return mod.MODKEY || ev == EV_ALT;

	/* Misc */
#ifdef DEBUG
	if (win && mod.MODKEY) {
		if (ev == EV_F1) return raise_float(win), 1;
		if (ev == EV_F2) return set_focus(win), 1;
		if (ev == EV_F3) return sys_show(win, ST_SHOW),  1;
		if (ev == EV_F4) return sys_show(win, ST_HIDE),  1;
		if (ev == EV_F7) return sys_show(win, ST_SHADE), 1;
	}
#endif
	if (mod.MODKEY) {
		if (ev == EV_F5) return wm_update(),    1;
		if (ev == EV_F6) return print_txt(),    1;
		if (ev == 'q')   return sys_exit(),     1;
	}
	if (win && mod.MODKEY) {
		if (ev == 'f')   return wm_handle_state(win, win->state,
			win->state == ST_FULL ? ST_SHOW : ST_FULL);
		if (ev == 'g')   return wm_handle_state(win, win->state,
			win->state == ST_MAX  ? ST_SHOW : ST_MAX);
	}
	if (win && mod.MODKEY && mod.shift) {
		if (ev == 'c')   return sys_show(win, ST_CLOSE), 1;
	}

	/* Floating layer */
	if (ev == ' ') {
		if (win && mod.MODKEY && mod.shift)
			return set_layer(win), 1;
		if (mod.MODKEY)
			return switch_layer(), 1;
	}

	/* Movement commands */
	if (mod.MODKEY && mod.shift) {
		switch (ev) {
		case 'h': return shift_window(wm_focus,-1, 0), 1;
		case 'j': return shift_window(wm_focus, 0,+1), 1;
		case 'k': return shift_window(wm_focus, 0,-1), 1;
		case 'l': return shift_window(wm_focus,+1, 0), 1;
		default: break;
		}
	}
	else if (mod.MODKEY) {
		switch (ev) {
		case 'h': return shift_focus(-1, 0), 1;
		case 'j': return shift_focus( 0,+1), 1;
		case 'k': return shift_focus( 0,-1), 1;
		case 'l': return shift_focus(+1, 0), 1;
		default: break;
		}
	}

	/* Column layout commands */
	if (mod.MODKEY) {
		switch (ev) {
		case 'd': return set_mode(win, SPLIT), 1;
		case 's': return set_mode(win, STACK), 1;
		case 'm': return set_mode(win, FULL),  1;
		case 't': return set_mode(win, TAB),   1;
		default: break;
		}
	}

	/* Tag switching */
	if (mod.MODKEY && '0' <= ev && ev <= '9') {
		char name[] = {ev, '\0'};
		if (win && mod.shift)
			tag_set(win, name);
		if (!mod.shift)
			tag_switch(name);
		wm_update();
	}

	/* Focus change */
	if (win && ev == EV_ENTER && win->state != ST_SHADE)
		return set_focus(win), 1;

	/* Reset focus after after focus change,
	 * not sure what is causing the focus change in the first place
	 * but preventing that would be a better solution */
	if (ev == EV_FOCUS)
		sys_focus(wm_focus);

	return mod.MODKEY;
}

int wm_handle_ptr(win_t *cwin, ptr_t ptr)
{
	//printf("wm_handle_ptr: %p - %d,%d %d,%d (%d) -- \n",
	//		cwin, ptr.x, ptr.y, ptr.rx, ptr.ry, move_mode);

	if (move_mode == NONE)
		return 0;

	int dx = ptr.rx - move_prev.rx;
	int dy = ptr.ry - move_prev.ry;
	move_prev = ptr;

	if (move_layer == TILING && move_mode == RESIZE) {
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

	if (move_layer == FLOATING) {
		flt_t *flt = move_lflt->data;
		win_t *win = flt->win;
		if (move_mode == MOVE)
			sys_move(win, win->x+dx, win->y+dy, win->w, win->h);
		else if (move_mode == RESIZE)
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

int wm_handle_state(win_t *win, state_t prev, state_t next)
{
	row_t *row = NULL;
	flt_t *flt = NULL;

	printf("wm_handle_state - %p %x -> %x\n", win, prev, next);

	search(wm_tag, win, NULL, NULL, &row, &flt);

	if (!row && !flt && next == ST_SHOW)
		return wm_insert(win), 1;
	if ((row || flt) && (next == ST_HIDE || next == ST_ICON))
		return wm_remove(win), 1;

	if (row) row->state = next;
	if (flt) flt->state = next;

	if (prev == ST_MAX || prev == ST_FULL ||
	    next == ST_MAX || next == ST_FULL)
		wm_update();

	return 1;
}

void wm_insert(win_t *win)
{
	printf("wm_insert: %p\n", win);

	/* Make sure it's visible */
	if (win->state == ST_HIDE)
		return;

	/* Check for toolbars */
	if (win->type == TYPE_TOOLBAR)
		return wm_update();

	print_txt();

	/* Initialize window */
	sys_watch(win, EV_ENTER, MOD());
	sys_watch(win, EV_FOCUS, MOD());

	/* Add to screen */
	if (win->type == TYPE_DIALOG || win->parent)
		wm_dpy->layer = FLOATING;
	put_win(win, wm_tag, wm_dpy->layer);

	/* Arrange */
	wm_update();
	set_focus(win);
	print_txt();
}

void wm_remove(win_t *win)
{
	printf("wm_remove: %p\n", win);
	print_txt();
	if (win->type == TYPE_TOOLBAR)
		return wm_update();
	for (list_t *tag = wm->tags; tag; tag = tag->next)
		cut_win(win, tag->data);
	set_focus(wm_focus);
	wm_update();
	print_txt();
}

void wm_init(void)
{
	printf("wm_init\n");

	/* Load configuration */
	margin = conf_get_int("main.margin", margin);
	stack  = conf_get_int("main.stack",  stack);

	wm          = new0(wm_t);
	wm->screens = list_sort(sys_info(), 0, sort_win);
	wm->tag     = tag_new(wm->screens, "1");
	wm->tags    = list_insert(NULL, wm->tag);

	event_t ev_e[] = {EV_ENTER, EV_FOCUS};
	event_t ev_s[] = {'h', 'j', 'k', 'l', 'c', 'q', ' ',
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
		EV_MOUSE1, EV_MOUSE3};
	event_t ev_m[] = {'h', 'j', 'k', 'l', 'd', 's', 'm', 't', 'f', 'g', ' ',
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
		EV_F1, EV_F2, EV_F3, EV_F4,  EV_F5,  EV_F6,
		EV_F7, EV_F8, EV_F9, EV_F10, EV_F11, EV_F12,
		EV_MOUSE1, EV_MOUSE3};
	for (int i = 0; i < countof(ev_e); i++)
		sys_watch(NULL, ev_e[i],  MOD());
	for (int i = 0; i < countof(ev_m); i++)
		sys_watch(NULL, ev_m[i], MOD(.MODKEY=1));
	for (int i = 0; i < countof(ev_s); i++)
		sys_watch(NULL, ev_s[i], MOD(.MODKEY=1,.shift=1));
}

void wm_free(void)
{
	/* Re-show and free all windows */
	while ( wm->tags) { tag_t *tag =  wm->tags->data;
	while (tag->dpys) { dpy_t *dpy = tag->dpys->data;
	while (dpy->cols) { col_t *col = dpy->cols->data;
	while (col->rows) { row_t *row = col->rows->data;
		sys_show(row->win, ST_SHOW);
	col->rows = list_remove(col->rows, col->rows, 1); }
	dpy->cols = list_remove(dpy->cols, dpy->cols, 1); }
	while (dpy->flts) { flt_t *flt = dpy->flts->data;
		sys_show(flt->win, ST_SHOW);
	dpy->flts = list_remove(dpy->flts, dpy->flts, 1); }
	tag->dpys = list_remove(tag->dpys, tag->dpys, 1); }
	 wm->tags = list_remove( wm->tags,  wm->tags, 1); }

	/* Free remaining data */
	free(wm);
}
