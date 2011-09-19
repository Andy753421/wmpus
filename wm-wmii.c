#include <stdio.h>
#include <stdlib.h>

#include "util.h"
#include "sys.h"
#include "wm.h"

#define MODKEY alt
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
	int     width;
	mode_t  mode;
	win_t  *focus;
	list_t *rows;
} col_t;

/* Mouse drag data */
static drag_t move_mode;
static win_t *move_win;
static ptr_t  move_prev;

/* Window management data */
static win_t  *wm_focus;
static list_t *wm_cols;
static win_t  *wm_root;

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
			row->h = wm_root->h;
		}
	wm_update();
}

static void set_focus(win_t *win)
{
	if (win->wm && win->wm->col)
		((col_t*)win->wm->col->data)->focus = win;

	/* - Only grab mouse button on unfocused window,
	 *   this prevents stealing all mouse clicks from client windows,
	 * - A better way may be to re-send mouse clicks to client windows
	 *   using the return value from wm_handle_key */
	for (int i = key_mouse1; i < key_mouse7; i++) {
		if (wm_focus)
			sys_watch(wm_focus, i, MOD());
		sys_unwatch(win, i, MOD());
	}

	wm_focus = win;
	sys_focus(win);
}

static void set_move(drag_t drag, win_t *win, ptr_t ptr)
{
	printf("set_move: %d - %p@%d,%d\n",
			drag, win, ptr.rx, ptr.ry);
	move_mode = drag;
	if (drag == move || drag == resize) {
		move_win  = win;
		move_prev = ptr;
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
				col->width, col->mode, col->focus);
		for (list_t *lrow = col->rows; lrow; lrow = lrow->next) {
			win_t *win = lrow->data;
			printf("  win:\t^%-9p <%-9p [%p=%p] >%-9p  -  %4dpx focus=%d%d\n",
					win->wm->col->data,
					(win->wm->row->prev ? win->wm->row->prev->data : NULL),
					win->wm->row->data, win,
					(win->wm->row->next ? win->wm->row->next->data : NULL),
					win->h, col->focus == win, wm_focus == win);
		}
	}
}

static void cut_window(win_t *win)
{
	list_t *lrow = win->wm->row;
	list_t *lcol = win->wm->col;
	col_t  *col  = lcol->data;

	col->focus = lrow->prev ? lrow->prev->data  :
	             lrow->next ? lrow->next->data  : NULL;

	wm_focus   = col->focus ? col->focus        :
	             lcol->prev ? ((col_t*)lcol->prev->data)->focus :
	             lcol->next ? ((col_t*)lcol->next->data)->focus : NULL;

	col->rows  = list_remove(col->rows, lrow);
	if (col->rows == NULL && (lcol->next || lcol->prev))
		wm_cols = list_remove(wm_cols, lcol);
}

static void put_window(win_t *win, list_t *lcol)
{
	if (lcol == NULL)
		lcol = wm_cols = list_insert(wm_cols, new0(col_t));

	col_t *col = lcol->data;
	int nrows = list_length(col->rows);
	if (col->focus) {
		list_insert_after(col->focus->wm->row, win);
		win->wm->row = col->focus->wm->row->next;
	} else {
		col->rows = list_insert(col->rows, win);
		win->wm->row = col->rows;
	}
	win->wm->col = lcol;
	col->focus   = win;
	wm_focus     = win;

	win->h = wm_root->h / MAX(nrows,1);
	if (nrows == 0) {
		int ncols = list_length(wm_cols);
		col->width = wm_root->w / MAX(ncols-1,1);
	}
}

static void shift_window(win_t *win, int col, int row)
{
	printf("shift_window: %p - %+d,%+d\n", win, col, row);
	print_txt(wm_cols);
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
				wm_cols = list_insert(wm_cols, new0(col_t));
			dst = src->prev;
		}
		if (col > 0) {
			if (!src->next && !onlyrow)
				wm_cols = list_append(wm_cols, new0(col_t));
			dst = src->next;
		}
		if (src && dst) {
			cut_window(win);
			put_window(win, dst);
			wm_update();
		}
	}
	print_txt(wm_cols);
}

static void shift_focus(win_t *win, int col, int row)
{
	printf("shift_focus: %p - %+d,%+d\n", win, col, row);
	list_t *node  = NULL;
	int update = 0;
	if (row != 0) {
		if (row < 0) node = win->wm->row->prev;
		if (row > 0) node = win->wm->row->next;
		if (((col_t*)win->wm->col->data)->mode != split)
			update = 1;
	} else {
		if (col < 0) node = win->wm->col->prev;
		if (col > 0) node = win->wm->col->next;
		if (node) {
			col_t *col = node->data;
			node = col->focus->wm->row;
		}
	}
	if (node)
		set_focus(node->data);
	if (update)
		wm_update();
}

/* Window management functions */
void wm_update(void)
{
	int  x=0,  y=0; // Current window top-left position
	int tx=0, ty=0; // Total x/y size
	int mx=0, my=0; // Maximum x/y size (screen size)
	int       sy=0; // Size of focused stack window

	/* Scale horizontally */
	x  = wm_root->x;
	mx = wm_root->w - (list_length(wm_cols)+1)*MARGIN;
	for (list_t *lx = wm_cols; lx; lx = lx->next)
		tx += ((col_t*)lx->data)->width;
	for (list_t *lx = wm_cols; lx; lx = lx->next)
		((col_t*)lx->data)->width *= (float)mx / tx;

	/* Scale each column vertically */
	for (list_t *lx = wm_cols; lx; lx = lx->next) {
		col_t *col = lx->data;
		ty = 0;
		for (list_t *ly = col->rows; ly; ly = ly->next)
			ty += ((win_t*)ly->data)->h;
		y  = wm_root->y;
		my = wm_root->h - (list_length(col->rows)+1)*MARGIN;
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
				height = col->focus == win ? sy : STACK;
				sys_move(win, x+MARGIN, y+MARGIN,
					col->width, height);
				break;
			case max:
			case tab:
				sys_move(win, x+MARGIN, 0+MARGIN,
					col->width, wm_root->h-2*MARGIN);
				if (col->focus == win)
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
	if (!win || win == wm_root) return 0;
	//printf("wm_handle_key: %p - %x %x\n", win, key, mod);

	/* Raise */
	if (key == key_f2)
		return set_focus(win), 1;
	if (key == key_f4)
		return sys_raise(win), 1;
	if (key == key_f1 && mod.MODKEY)
		sys_raise(win);
	if (key == key_f12 && mod.MODKEY)
		print_txt(wm_cols);
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

	/* Mouse movement */
	if (key_mouse0 <= key && key <= key_mouse7 && mod.up)
		return set_move(none,win,ptr), 1;
	else if (key == key_mouse1 && mod.MODKEY)
		return set_move(move,win,ptr), 1;
	else if (key == key_mouse3 && mod.MODKEY)
		return set_move(resize,win,ptr), 1;

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
		list_t *row   = move_win->wm->row;
		list_t *col   = move_win->wm->col;
		list_t *lower = row->next;
		list_t *right = col->next;
		if (lower) {
			((win_t*)row->data)->h       += dy;
			((win_t*)lower->data)->h     -= dy;
		}
		if (right) {
			((col_t*)col->data)->width   += dx;
			((col_t*)right->data)->width -= dx;
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
	print_txt(wm_cols);

	/* Initialize window */
	win->wm = new0(win_wm_t);
	sys_watch(win, key_enter, MOD());
	sys_watch(win, key_focus, MOD());

	/* Add to screen */
	list_t *lcol = wm_focus && wm_focus->wm ?
		wm_focus->wm->col : wm_cols;
	put_window(win, lcol);

	/* Arrange */
	wm_update();
	sys_focus(wm_focus);
	print_txt(wm_cols);
}

void wm_remove(win_t *win)
{
	printf("wm_remove: %p - (%p,%p)\n", win,
			win->wm->col, win->wm->row);
	print_txt(wm_cols);
	cut_window(win);
	if (wm_focus)
		sys_focus(wm_focus);
	else
		sys_focus(wm_root);
	wm_update();
	print_txt(wm_cols);
}

void wm_init(win_t *root)
{
	printf("wm_init: %p\n", root);
	wm_root = root;
	sys_watch(root, key_f1,     MOD(.MODKEY=1));
	sys_watch(root, key_f12,    MOD(.MODKEY=1));
	sys_watch(root, key_mouse1, MOD(.MODKEY=1));
	sys_watch(root, key_mouse3, MOD(.MODKEY=1));
	sys_watch(root, key_enter,  MOD());
	sys_watch(root, key_focus,  MOD());
	Key_t keys_m[] = {'h', 'j', 'k', 'l', 'd', 's', 'm', 't'};
	Key_t keys_s[] = {'h', 'j', 'k', 'l'};
	for (int i = 0; i < countof(keys_m); i++)
		sys_watch(root, keys_m[i], MOD(.MODKEY=1));
	for (int i = 0; i < countof(keys_s); i++)
		sys_watch(root, keys_s[i], MOD(.MODKEY=1,.shift=1));
}
