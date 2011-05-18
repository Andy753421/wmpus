#include <stdio.h>

#include "sys.h"
#include "wm.h"

typedef enum {
	none, move, resize
} Mode_t;

win_t *kwin;
ptr_t  kptr;
Mode_t mode;

int wm_handle_key(win_t *win, Key_t key, mod_t mod, ptr_t ptr)
{
	printf("wm_handle_key: %p - %x\n", win, key);
	kptr = ptr;
	kwin = win;

	/* Raise */
	if ((key == key_f1 && mod.ctrl) ||
			(key_mouse0 <= key && key <= key_mouse7))
		sys_raise(win);

	/* Movement */
	if (key_mouse0 <= key && key <= key_mouse7 &&
			mod.up && mode != none)
		return mode = none, 1;
	else if (key == key_mouse1 && mod.ctrl)
		return mode = move, 1;
	else if (key == key_mouse3 && mod.ctrl)
		return mode = resize, 1;

	return 0;
}

int wm_handle_ptr(win_t *win, ptr_t ptr)
{
	printf("wm_handle_ptr: %p - %d,%d %d,%d (%d) -- \n",
			win, ptr.x, ptr.y, ptr.rx, ptr.ry, mode);
	int dx = ptr.rx - kptr.rx;
	int dy = ptr.ry - kptr.ry;
	if (mode == move)
		sys_move(kwin, kwin->x+dx, kwin->y+dy, kwin->w, kwin->h);
	else if (mode == resize)
		sys_move(kwin, kwin->x, kwin->y, kwin->w+dx, kwin->h+dy);
	return 0;
}

void wm_init(win_t *root)
{
	sys_watch(root, key_f1,     MOD(.ctrl=1));
	sys_watch(root, key_mouse1, MOD(.ctrl=1));
	sys_watch(root, key_mouse3, MOD(.ctrl=1));
}
