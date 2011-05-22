#include <stdio.h>
#include <stdlib.h>

#include "util.h"
#include "sys.h"
#include "wm.h"

#define MODKEY ctrl

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
	list_t *wins;
} col_t;

/* Mouse drag data */
drag_t move_mode;
win_t *move_win;
ptr_t  move_prev;

/* Window management data */
list_t *wm_cols;
win_t  *wm_root;

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
		for (list_t *wnode = col->wins; wnode; wnode = wnode->next) {
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
		for (list_t *ly = col->wins; ly; ly = ly->next)
			ty += ((win_t*)ly->data)->h;
		y = 0;
		for (list_t *ly = col->wins; ly; ly = ly->next) {
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
		if (node) node = ((col_t*)node->data)->wins;
	}
	if (node)
		sys_focus(node->data);
}

/* Window management functions */
int wm_handle_key(win_t *win, Key_t key, mod_t mod, ptr_t ptr)
{
	if (!win) return 0;
	//printf("wm_handle_key: %p - %x\n", win, key);

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
		case 'h': case 'H': return shift_window(win,-1, 0), 1;
		case 'k': case 'K': return shift_window(win, 0,-1), 1;
		case 'j': case 'J': return shift_window(win,+1, 0), 1;
		case 'l': case 'L': return shift_window(win, 0,+1), 1;
		default: break;
		}
	}
	else if (mod.MODKEY) {
		switch (key) {
		case 'h': case 'H': return shift_focus(win,-1, 0), 1;
		case 'k': case 'K': return shift_focus(win, 0,-1), 1;
		case 'j': case 'J': return shift_focus(win,+1, 0), 1;
		case 'l': case 'L': return shift_focus(win, 0,+1), 1;
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

	return 0;
}

int wm_handle_ptr(win_t *cwin, ptr_t ptr)
{
	//printf("wm_handle_ptr: %p - %d,%d %d,%d (%d) -- \n",
	//		cwin, ptr.x, ptr.y, ptr.rx, ptr.ry, move_mode);
	if (move_mode == none)
		return 0;
	win_t *mwin = move_win;
	int dx = ptr.rx - move_prev.rx;
	int dy = ptr.ry - move_prev.ry;
	move_prev = ptr;
	if (move_mode == move)
		sys_move(mwin, mwin->x+dx, mwin->y+dy, mwin->w, mwin->h);
	else if (move_mode == resize)
		sys_move(mwin, mwin->x, mwin->y, mwin->w+dx, mwin->h+dy);
	return 0;
}

void wm_insert(win_t *win)
{
	printf("wm_insert: %p\n", win);
	print_txt(wm_cols);

	/* Add to screen */
	col_t *col = wm_cols->data;
	int nwins = list_length(col->wins);
	col->wins = list_insert(col->wins, win);

	/* Setup window */
	win->wm = new0(win_wm_t);
	win->wm->col = wm_cols;
	win->wm->row = col->wins;
	if (nwins) win->h = wm_root->h / nwins;

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
	col->wins = list_remove(col->wins, win->wm->row);
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
	key_t keys[] = {'h', 'j', 'k', 'l', 'H', 'J', 'K', 'L'};
	for (int i = 0; i < countof(keys); i++)
		sys_watch(root, keys[i], MOD(.MODKEY=1));
	col_t *col = new0(col_t);
	col->group = stack;
	col->width = root->w;
	wm_cols = list_insert(wm_cols, col);
}
