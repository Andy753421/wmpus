#include <stdio.h>
#include <stdlib.h>

#include "util.h"
#include "sys.h"
#include "wm.h"

#define MODKEY ctrl
#define MARGIN 0
#define STACK  25

/* Loca types */
struct win_wm {
	list_t *col; // node in wm_cols
	list_t *row; // node in col->rows
};

typedef enum {
	none, move, resize
} drag_t;

typedef enum {
	split, stack, max, tab
} mode_t;

typedef struct {
	list_t *rows; // of win_t
	win_t  *row;
	int     width;
	mode_t  mode;
} col_t;

typedef struct {
	list_t *cols; // of col_t
	col_t  *col;
	win_t  *root;
} dpy_t;

typedef struct {
	list_t *dpys; // of dpy_t
	dpy_t  *dpy;
	int     name;
} tag_t;

typedef struct {
	list_t *tags; // of tag_t
	tag_t  *tag;
} wm_t;

/* Mouse drag data */
static drag_t move_mode;
static win_t *move_win;
static ptr_t  move_prev;
static struct { int v, h; } move_dir;

/* Window management data */
static wm_t  *wm;
#define wm_focus (wm->tag->dpy->col ? wm->tag->dpy->col->row : NULL)
#define wm_dpy wm->tag->dpy

/* Helper functions */
static void set_mode(win_t *win, mode_t mode)
{
	if (!win->wm || !win->wm->col)
		return;
	col_t *col = win->wm->col->data;
	printf("set_mode: %p (%p), %d -> %d\n",
			win, col, col->mode, mode);
	col->mode = mode;
	if (col->mode == split)
		for (list_t *cur = col->rows; cur; cur = cur->next) {
			win_t *row = cur->data;
			row->h = wm_dpy->root->h;
		}
	wm_update();
}

static void set_focus(win_t *win)
{
	/* - Only grab mouse button on unfocused window,
	 *   this prevents stealing all mouse clicks from client windows,
	 * - A better way may be to re-send mouse clicks to client windows
	 *   using the return value from wm_handle_key */
	for (int i = key_mouse1; i < key_mouse7; i++) {
		if (wm_focus)
			sys_watch(wm_focus, i, MOD());
		sys_unwatch(win, i, MOD());
	}

	if (win->wm && win->wm->col)
		((col_t*)win->wm->col->data)->row = win;
	sys_focus(win);
}

static void set_move(win_t *win, ptr_t ptr, drag_t drag)
{
	printf("set_move: %d - %p@%d,%d\n",
			drag, win, ptr.rx, ptr.ry);
	move_mode = drag;
	if (drag == move || drag == resize) {
		move_win  = win;
		move_prev = ptr;
		int my = win->y + (win->h/2);
		int mx = win->x + (win->w/2);
		move_dir.v = ptr.ry < my ? -1 : +1;
		move_dir.h = ptr.rx < mx ? -1 : +1;
	}
}

static void print_txt(list_t *cols)
{
	for (list_t *lcol = cols; lcol; lcol = lcol->next) {
		col_t *col = lcol->data;
		printf("col:\t           <%-9p [%-19p] >%-9p  -  %dpx @ %d !!%p\n",
				( lcol->prev ? lcol->prev->data : NULL ),
				col,
				( lcol->next ? lcol->next->data : NULL ),
				col->width, col->mode, col->row);
		for (list_t *lrow = col->rows; lrow; lrow = lrow->next) {
			win_t *win = lrow->data;
			printf("  win:\t^%-9p <%-9p [%p=%p] >%-9p  -  %4dpx focus=%d%d\n",
					win->wm->col->data,
					(win->wm->row->prev ? win->wm->row->prev->data : NULL),
					win->wm->row->data, win,
					(win->wm->row->next ? win->wm->row->next->data : NULL),
					win->h, col->row == win, wm_focus == win);
		}
	}
}

static void cut_window(win_t *win)
{
	list_t *lrow = win->wm->row;
	list_t *lcol = win->wm->col;
	col_t  *col  = lcol->data;
	dpy_t  *dpy  = wm_dpy; // fixme

	col->row  = lrow->prev ? lrow->prev->data :
	            lrow->next ? lrow->next->data : NULL;
	col->rows = list_remove(col->rows, lrow);

	if (col->rows == NULL && (lcol->next || lcol->prev)) {
		dpy->col  = lcol->prev ? lcol->prev->data :
			    lcol->next ? lcol->next->data : NULL;
		dpy->cols = list_remove(wm_dpy->cols, lcol);
	}
}

static void put_window(win_t *win, list_t *lcol)
{
	if (lcol == NULL)
		lcol = wm_dpy->cols = list_insert(wm_dpy->cols, new0(col_t));

	col_t *col = lcol->data;
	int nrows = list_length(col->rows);
	if (col->row) {
		list_insert_after(col->row->wm->row, win);
		win->wm->row = col->row->wm->row->next;
	} else {
		col->rows = list_insert(col->rows, win);
		win->wm->row = col->rows;
	}
	win->wm->col = lcol;
	col->row     = win;
	wm_dpy->col  = col;

	win->h = wm_dpy->root->h / MAX(nrows,1);
	if (nrows == 0) {
		int ncols = list_length(wm_dpy->cols);
		col->width = wm_dpy->root->w / MAX(ncols-1,1);
	}
}

static void shift_window(win_t *win, int col, int row)
{
	printf("shift_window: %p - %+d,%+d\n", win, col, row);
	print_txt(wm_dpy->cols);
	printf("shift_window: >>>\n");
	if (row != 0) {
		list_t *src = win->wm->row, *dst = NULL;
		if (row < 0) dst = src->prev;
		if (row > 0) dst = src->next;
		if (src && dst) {
			printf("swap: %p <-> %p\n", src->data, dst->data);
			src->data = dst->data;
			dst->data = win;
			((win_t*)src->data)->wm->row = src;
			((win_t*)dst->data)->wm->row = dst;
			wm_update();
		}
	} else {
		int onlyrow = !win->wm->row->prev && !win->wm->row->next;
		list_t *src = win->wm->col, *dst = NULL;
		if (col < 0) {
			if (!src->prev && !onlyrow)
				wm_dpy->cols = list_insert(wm_dpy->cols, new0(col_t));
			dst = src->prev;
		}
		if (col > 0) {
			if (!src->next && !onlyrow)
				wm_dpy->cols = list_append(wm_dpy->cols, new0(col_t));
			dst = src->next;
		}
		if (src && dst) {
			cut_window(win);
			put_window(win, dst);
			wm_update();
		}
	}
	print_txt(wm_dpy->cols);
}

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
static void shift_focus(win_t *win, int col, int row)
{
	printf("shift_focus: %p - %+d,%+d\n", win, col, row);
	if (row != 0) {
		set_focus(get_next(win->wm->row, row > 0)->data);
		if (((col_t*)win->wm->col->data)->mode != split)
			wm_update();
	}
	if (col != 0) {
		col_t *next = get_next(win->wm->col, col > 0)->data;
		set_focus(next->row->wm->row->data);
	}
}

/* Window management functions */
void wm_update(void)
{
	int  x=0,  y=0; // Current window top-left position
	int tx=0, ty=0; // Total x/y size
	int mx=0, my=0; // Maximum x/y size (screen size)
	int       sy=0; // Size of focused stack window

	/* Scale horizontally */
	x  = wm_dpy->root->x;
	mx = wm_dpy->root->w - (list_length(wm_dpy->cols)+1)*MARGIN;
	for (list_t *lx = wm_dpy->cols; lx; lx = lx->next)
		tx += ((col_t*)lx->data)->width;
	for (list_t *lx = wm_dpy->cols; lx; lx = lx->next)
		((col_t*)lx->data)->width *= (float)mx / tx;

	/* Scale each column vertically */
	for (list_t *lx = wm_dpy->cols; lx; lx = lx->next) {
		col_t *col = lx->data;
		ty = 0;
		for (list_t *ly = col->rows; ly; ly = ly->next)
			ty += ((win_t*)ly->data)->h;
		y  = wm_dpy->root->y;
		my = wm_dpy->root->h - (list_length(col->rows)+1)*MARGIN;
		sy = my         - (list_length(col->rows)-1)*STACK;
		for (list_t *ly = col->rows; ly; ly = ly->next) {
			win_t *win = ly->data;
			int height = 0;
			switch (col->mode) {
			case split:
				sys_move(win, x+MARGIN, y+MARGIN,
					col->width, win->h * ((float)my / ty));
				height = win->h;
				break;
			case stack:
				height = col->row == win ? sy : STACK;
				sys_move(win, x+MARGIN, y+MARGIN,
					col->width, height);
				break;
			case max:
			case tab:
				sys_move(win, x+MARGIN, 0+MARGIN,
					col->width, wm_dpy->root->h-2*MARGIN);
				if (col->row == win)
					sys_raise(win);
				break;
			}
			y += height + MARGIN;
		}
		x += col->width + MARGIN;
	}
}

int wm_handle_key(win_t *win, Key_t key, mod_t mod, ptr_t ptr)
{
	if (!win || win == wm_dpy->root) return 0;
	//printf("wm_handle_key: %p - %x %c%c%c%c%c\n", win, key,
	//	mod.up    ? '^' : 'v',
	//	mod.alt   ? 'a' : '-',
	//	mod.ctrl  ? 'c' : '-',
	//	mod.shift ? 's' : '-',
	//	mod.win   ? 'w' : '-');

	/* Mouse movement */
	if (key_mouse0 <= key && key <= key_mouse7 && mod.up)
		return set_move(win,ptr,none), 1;
	else if (key == key_mouse1 && mod.MODKEY)
		return set_move(win,ptr,move), 1;
	else if (key == key_mouse3 && mod.MODKEY)
		return set_move(win,ptr,resize), 1;

	/* Only handle key-down */
	if (mod.up)
		return 0;

	/* Misc */
	if (mod.MODKEY) {
		if (key == key_f1) return sys_raise(win), 1;
		if (key == key_f2) return set_focus(win), 1;
		if (key == key_f5) return wm_update(),    1;
		if (key == key_f6) return print_txt(wm_dpy->cols), 1;
	}
	if (key_mouse0 <= key && key <= key_mouse7)
		sys_raise(win);

	/* Movement commands */
	if (mod.MODKEY && mod.shift) {
		switch (key) {
		case 'h': return shift_window(win,-1, 0), 1;
		case 'j': return shift_window(win, 0,+1), 1;
		case 'k': return shift_window(win, 0,-1), 1;
		case 'l': return shift_window(win,+1, 0), 1;
		default: break;
		}
	}
	else if (mod.MODKEY) {
		switch (key) {
		case 'h': return shift_focus(win,-1, 0), 1;
		case 'j': return shift_focus(win, 0,+1), 1;
		case 'k': return shift_focus(win, 0,-1), 1;
		case 'l': return shift_focus(win,+1, 0), 1;
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

	/* Focus change */
	if (key == key_enter)
		return set_focus(win), 1;

	if (key_mouse0 <= key && key <= key_mouse7)
		return set_focus(win), 0;

	/* Reset focus after after focus change,
	 * not sure what is causing the focus change in the first place
	 * but preventing that would be a better solution */
	if (key == key_focus)
		set_focus(wm_focus);

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
		list_t *row  = move_win->wm->row;
		list_t *col  = move_win->wm->col;
		list_t *vert = move_dir.v < 0 ? row->prev : row->next;
		list_t *horz = move_dir.h < 0 ? col->prev : col->next;
		if (vert) {
			((win_t*)row->data)->h      += move_dir.v * dy;
			((win_t*)vert->data)->h     -= move_dir.v * dy;
		}
		if (horz) {
			((col_t*)col->data)->width  += move_dir.h * dx;
			((col_t*)horz->data)->width -= move_dir.h * dx;
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
	print_txt(wm_dpy->cols);

	/* Initialize window */
	win->wm = new0(win_wm_t);
	sys_watch(win, key_enter, MOD());
	sys_watch(win, key_focus, MOD());

	/* Add to screen */
	list_t *lcol = wm_focus && wm_focus->wm ?
		wm_focus->wm->col : wm_dpy->cols;
	put_window(win, lcol);

	/* Arrange */
	wm_update();
	sys_focus(wm_focus);
	print_txt(wm_dpy->cols);
}

void wm_remove(win_t *win)
{
	printf("wm_remove: %p - (%p,%p)\n", win,
			win->wm->col, win->wm->row);
	print_txt(wm_dpy->cols);
	cut_window(win);
	if (wm_focus)
		sys_focus(wm_focus);
	else
		sys_focus(wm_dpy->root);
	wm_update();
	print_txt(wm_dpy->cols);
}

void wm_init(win_t *root)
{
	printf("wm_init: %p\n", root);
	       wm  = new0(wm_t);
	tag_t *tag = new0(tag_t);
	dpy_t *dpy = new0(dpy_t);

	dpy->root  = root;
	tag->dpys  = list_insert(NULL, dpy);
	tag->dpy   = dpy;
	tag->name  = 1;
	wm->tags   = list_insert(NULL, tag);
	wm->tag    = tag;

	Key_t keys_e[] = {key_enter, key_focus};
	Key_t keys_s[] = {'h', 'j', 'k', 'l'};
	Key_t keys_m[] = {'h', 'j', 'k', 'l', 'd', 's', 'm', 't',
		key_f1, key_f2, key_f5, key_f6, key_mouse1, key_mouse3};
	for (int i = 0; i < countof(keys_e); i++)
		sys_watch(root, keys_e[i],  MOD());
	for (int i = 0; i < countof(keys_m); i++)
		sys_watch(root, keys_m[i], MOD(.MODKEY=1));
	for (int i = 0; i < countof(keys_s); i++)
		sys_watch(root, keys_s[i], MOD(.MODKEY=1,.shift=1));
}
