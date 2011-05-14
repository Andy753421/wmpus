#include <stdio.h>

#include "sys.h"
#include "wm.h"

typedef enum {
	none, move, resize
} mode_t;

win_t *kwin;
ptr_t  kptr;
mode_t mode;

void wm_handle_key(win_t *win, Key_t key, mod_t mod, ptr_t ptr)
{
	printf("wm_handle_key: %p - %x %x\n", win, key, *(int*)&mod);
	kptr = ptr;
	kwin = win;
	if (key == key_f1 && mod.ctrl)
		sys_raise(win);
	else if (key_mouse0 <= key && key <= key_mouse7 && mod.up)
		mode = none;
	else if (key == key_mouse1)
		mode = move;
	else if (key == key_mouse3)
		mode = resize;
}

void wm_handle_ptr(win_t *win, ptr_t ptr)
{
	printf("wm_handle_ptr: %p - %d,%d (%d)\n", win, ptr.x, ptr.y, mode);
	int dx = ptr.rx - kptr.rx;
	int dy = ptr.ry - kptr.ry;
	if (mode == move)
		sys_move(kwin, kwin->x+dx, kwin->y+dy, kwin->w, kwin->h);
	else if (mode == resize)
		sys_move(kwin, kwin->x, kwin->y, kwin->w+dx, kwin->h+dy);
}

void wm_init(win_t *root)
{
	sys_watch(root, key_f1,     MOD(.ctrl=1));
	sys_watch(root, key_mouse1, MOD(.ctrl=1));
	sys_watch(root, key_mouse3, MOD(.ctrl=1));
}
