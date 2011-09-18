#include <stdio.h>
#include <stdlib.h>

#include "util.h"
#include "sys.h"
#include "wm.h"

#define MODKEY alt

/* Loca types */
struct win_wm {
	list_t *col;
	list_t *row;
};

typedef enum {
	none, move, resize
} drag_t;

typedef enum {
	stack, split, max, tab
} group_t;

typedef struct {
	int     width;
	group_t group;
	list_t *rows;
} col_t;

/* Mouse drag data */
static drag_t move_mode;
static win_t *move_win;
static ptr_t  move_prev;

/* Window management data */
static list_t *wm_cols;
static win_t  *wm_root;

/* Helper functions */
static void set_mode(drag_t drag, win_t *win, ptr_t ptr)
{
	printf("set_mode: %d - %p@%d,%d\n",
			drag, win, ptr.rx, ptr.ry);
	move_mode = drag;
	if (drag == move || drag == resize) {
		move_win  = win;
		move_prev = ptr;
	}
}

static void print_txt(list_t *cols)
{
	for (list_t *cnode = cols; cnode; cnode = cnode->next) {
		col_t *col = cnode->data;
		printf("col:\t%p - %dpx @ %d\n", col, col->width, col->group);
		for (list_t *wnode = col->rows; wnode; wnode = wnode->next) {
			win_t *win = wnode->data;
			printf("  win:\t%p - %dpx\n", win, win->h);
		}
	}
}

static void arrange(list_t *cols)
{
	int  x=0,  y=0; // Current window top-left position
	int tx=0, ty=0; // Total x/y size
	int mx=0, my=0; // Maximum x/y size (screen size)

	mx = wm_root->w;
	my = wm_root->h;

	/* Scale horizontally */
	for (list_t *lx = cols; lx; lx = lx->next)
		tx += ((col_t*)lx->data)->width;
	for (list_t *lx = cols; lx; lx = lx->next)
		((col_t*)lx->data)->width *= (float)mx / tx;

	/* Scale each column vertically */
	for (list_t *lx = cols; lx; lx = lx->next) {
		col_t *col = lx->data;
		ty = 0;
		for (list_t *ly = col->rows; ly; ly = ly->next)
			ty += ((win_t*)ly->data)->h;
		y = 0;
		for (list_t *ly = col->rows; ly; ly = ly->next) {
			win_t *win = ly->data;
			sys_move(win, x, y, col->width,
				win->h * ((float)my / ty));
			y += win->h;
		}
		x += col->width;
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
			arrange(wm_cols);
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
			col_t *scol = src->data;
			col_t *dcol = dst->data;
			scol->rows = list_remove(scol->rows, win->wm->row);
			dcol->rows = list_insert(dcol->rows, win);
			win->wm->row = dcol->rows;
			win->wm->col = dst;
			if (onlyrow) // delete column
				wm_cols = list_remove(wm_cols, src);
			if (dcol->width == 0) // new column
				dcol->width = wm_root->w / (list_length(wm_cols)-1);
			else
				win->h      = wm_root->h / (list_length(dcol->rows)-1);
			arrange(wm_cols);
		}
	}
	print_txt(wm_cols);
}

static void shift_focus(win_t *win, int col, int row)
{
	printf("shift_focus: %p - %+d,%+d\n", win, col, row);
	list_t *node  = NULL;
	if (row != 0) {
		if (row < 0) node = win->wm->row->prev;
		if (row > 0) node = win->wm->row->next;
	} else {
		if (col < 0) node = win->wm->col->prev;
		if (col > 0) node = win->wm->col->next;
		if (node) node = ((col_t*)node->data)->rows;
	}
	if (node)
		sys_focus(node->data);
}

/* Window management functions */
int wm_handle_key(win_t *win, Key_t key, mod_t mod, ptr_t ptr)
{
	if (!win) return 0;
	//printf("wm_handle_key: %p - %x %x\n", win, key, mod);

	/* Raise */
	if (key == key_f2)
		return sys_focus(win), 1;
	if (key == key_f4)
		return sys_raise(win), 1;
	if (key == key_f1 && mod.MODKEY)
		sys_raise(win);
	if (key_mouse0 <= key && key <= key_mouse7)
		sys_raise(win);

	/* Key commands */
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

	/* Mouse movement */
	if (key_mouse0 <= key && key <= key_mouse7 && mod.up)
		return set_mode(none,win,ptr), 1;
	else if (key == key_mouse1 && mod.MODKEY)
		return set_mode(move,win,ptr), 1;
	else if (key == key_mouse3 && mod.MODKEY)
		return set_mode(resize,win,ptr), 1;

	/* Focus change */
	if (key == key_enter)
		sys_focus(win);

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
		arrange(wm_cols);
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

	/* Watch enter/leave */
	sys_watch(win, key_enter, MOD());

	/* Add to screen */
	col_t *col = wm_cols->data;
	int nrows = list_length(col->rows);
	col->rows = list_insert(col->rows, win);

	/* Setup window */
	win->wm = new0(win_wm_t);
	win->wm->col = wm_cols;
	win->wm->row = col->rows;
	if (nrows) win->h = wm_root->h / nrows;

	/* Arrange */
	arrange(wm_cols);
	print_txt(wm_cols);
}

void wm_remove(win_t *win)
{
	printf("wm_remove: %p - (%p,%p)\n", win,
			win->wm->col, win->wm->row);
	print_txt(wm_cols);
	col_t *col = win->wm->col->data;
	col->rows = list_remove(col->rows, win->wm->row);
	arrange(wm_cols);
	print_txt(wm_cols);
}

void wm_init(win_t *root)
{
	printf("wm_init: %p\n", root);
	wm_root = root;
	sys_watch(root, key_f1,     MOD(.MODKEY=1));
	sys_watch(root, key_mouse1, MOD(.MODKEY=1));
	sys_watch(root, key_mouse3, MOD(.MODKEY=1));
	sys_watch(root, key_enter,  MOD());
	Key_t keys[] = {'h', 'j', 'k', 'l'};
	for (int i = 0; i < countof(keys); i++) {
		sys_watch(root, keys[i], MOD(.MODKEY=1));
		sys_watch(root, keys[i], MOD(.MODKEY=1,.shift=1));
	}
	col_t *col = new0(col_t);
	col->group = stack;
	col->width = root->w;
	wm_cols = list_insert(wm_cols, col);
}
