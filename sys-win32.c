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
#include <ctype.h>
#include <search.h>

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0500
#include <windows.h>
#include <winbase.h>
#include <winuser.h>

#include "util.h"
#include "sys.h"
#include "wm.h"

/* Internal structures */
struct win_sys {
	HWND hwnd;
	state_t state;
};

typedef struct {
	Key_t key;
	int   vk;
} keymap_t;

/* Global data */
static int    shellhookid;
static void  *cache;
static win_t *root;

/* Conversion functions */
static keymap_t key2vk[] = {
	{key_mouse1  , VK_LBUTTON },
	{key_mouse2  , VK_MBUTTON },
	{key_mouse3  , VK_RBUTTON },
	{key_left    , VK_LEFT    },
	{key_right   , VK_RIGHT   },
	{key_up      , VK_UP      },
	{key_down    , VK_DOWN    },
	{key_home    , VK_HOME    },
	{key_end     , VK_END     },
	{key_pageup  , VK_PRIOR   },
	{key_pagedown, VK_NEXT    },
	{key_f1      , VK_F1      },
	{key_f2      , VK_F2      },
	{key_f3      , VK_F3      },
	{key_f4      , VK_F4      },
	{key_f5      , VK_F5      },
	{key_f6      , VK_F6      },
	{key_f7      , VK_F7      },
	{key_f8      , VK_F8      },
	{key_f9      , VK_F9      },
	{key_f10     , VK_F10     },
	{key_f11     , VK_F11     },
	{key_f12     , VK_F12     },
	{key_shift   , VK_SHIFT   },
	{key_shift   , VK_LSHIFT  },
	{key_shift   , VK_RSHIFT  },
	{key_ctrl    , VK_CONTROL },
	{key_ctrl    , VK_LCONTROL},
	{key_ctrl    , VK_RCONTROL},
	{key_alt     , VK_MENU    },
	{key_alt     , VK_LMENU   },
	{key_alt     , VK_RMENU   },
	{key_win     , VK_LWIN    },
	{key_win     , VK_RWIN    },
};

/* - Keycodes */
static Key_t w2key(UINT vk)
{
	keymap_t *km = map_getr(key2vk,vk);
	return km ? km->key : tolower(vk);
}

static UINT key2w(Key_t key)
{
	keymap_t *km = map_get(key2vk,key);
	return km ? km->vk : toupper(key);
}

static mod_t getmod(void)
{
	return (mod_t){
		.alt   = GetKeyState(VK_MENU)    < 0,
		.ctrl  = GetKeyState(VK_CONTROL) < 0,
		.shift = GetKeyState(VK_SHIFT)   < 0,
		.win   = GetKeyState(VK_LWIN)    < 0 ||
		         GetKeyState(VK_RWIN)    < 0,
	};
}

/* - Pointers */
static ptr_t getptr(void)
{
	POINT wptr;
	GetCursorPos(&wptr);
	return (ptr_t){-1, -1, wptr.x, wptr.y};
}

/* Window functions */
static win_t *win_new(HWND hwnd, int checkwin)
{
	if (checkwin) {
		char winclass[256];
		int exstyle = GetWindowLong(hwnd, GWL_EXSTYLE);
		GetClassName(hwnd, winclass, sizeof(winclass));
		if (!IsWindowVisible(hwnd))      return NULL; // Invisible stuff..
		if (!strcmp("#32770", winclass)) return NULL; // Message boxes
		if (GetWindow(hwnd, GW_OWNER))   return NULL; // Dialog boxes, etc
		if (exstyle & WS_EX_TOOLWINDOW)  return NULL; // Floating toolbars
	}

	RECT rect = {};
	GetWindowRect(hwnd, &rect);
	win_t *win = new0(win_t);
	win->x         = rect.left;
	win->y         = rect.top;
	win->w         = rect.right  - rect.left;
	win->h         = rect.bottom - rect.top;
	win->sys       = new0(win_sys_t);
	win->sys->hwnd = hwnd;
	printf("win_new: %p = %p (%d,%d %dx%d)\n", win, hwnd,
			win->x, win->y, win->w, win->h);
	return win;
}

static int win_cmp(const void *_a, const void *_b)
{
	const win_t *a = _a, *b = _b;
	if (a->sys->hwnd < b->sys->hwnd) return -1;
	if (a->sys->hwnd > b->sys->hwnd) return  1;
	return 0;
}

static win_t *win_find(HWND hwnd, int create)
{
	if (!hwnd)
		return NULL;
	//printf("win_find: %p, %d\n", dpy, (int)xid);
	win_sys_t sys = {.hwnd=hwnd};
	win_t     tmp = {.sys=&sys};
	win_t **old = NULL, *new = NULL;
	if ((old = tfind(&tmp, &cache, win_cmp)))
		return *old;
	if (create && (new = win_new(hwnd,1)))
		tsearch(new, &cache, win_cmp);
	return new;
}

static void win_remove(win_t *win)
{
	tdelete(win, &cache, win_cmp);
	free(win->sys);
	free(win);
}

static win_t *win_cursor(void)
{
	POINT wptr;
	GetCursorPos(&wptr);
	return win_find(GetAncestor(WindowFromPoint(wptr),GA_ROOT),0);
}

static win_t *win_focused(void)
{
	return win_find(GetForegroundWindow(),0);
}

/* Callbacks */
LRESULT CALLBACK KbdProc(int msg, WPARAM wParam, LPARAM lParam)
{
	KBDLLHOOKSTRUCT *st = (KBDLLHOOKSTRUCT *)lParam;
	Key_t key = w2key(st->vkCode);
	mod_t mod = getmod();
	mod.up = !!(st->flags & 0x80);
	printf("KbdProc: %d,%x,%lx - %lx,%lx,%lx - %x,%x\n",
			msg, wParam, lParam,
			st->vkCode, st->scanCode, st->flags,
			key, mod2int(mod));
	return wm_handle_key(win_focused() ?: root, key, mod, getptr())
		|| CallNextHookEx(0, msg, wParam, lParam);
}

LRESULT CALLBACK MllProc(int msg, WPARAM wParam, LPARAM lParam)
{
	Key_t  key = key_none;
	mod_t  mod = getmod();
	win_t *win = win_cursor();

	/* Update modifiers */
	switch (wParam) {
	case WM_LBUTTONDOWN: mod.up = 0; key = key_mouse1; break;
	case WM_LBUTTONUP:   mod.up = 1; key = key_mouse1; break;
	case WM_RBUTTONDOWN: mod.up = 0; key = key_mouse3; break;
	case WM_RBUTTONUP:   mod.up = 1; key = key_mouse3; break;
	}

	/* Check for focus-in/focus-out */
	static win_t *old = NULL;
	if (win && win != old) {
		wm_handle_key(old, key_leave, mod, getptr());
		wm_handle_key(win, key_enter, mod, getptr());
	}
	old = win;

	/* Send mouse movement event */
	if (wParam == WM_MOUSEMOVE)
		return wm_handle_ptr(win_cursor(), getptr());
	else if (key != key_none)
		return wm_handle_key(win_cursor(), key, mod, getptr());
	else
		return CallNextHookEx(0, msg, wParam, lParam);
}

LRESULT CALLBACK ShlProc(int msg, WPARAM wParam, LPARAM lParam)
{
	HWND hwnd = (HWND)wParam;
	win_t *win = NULL;
	switch (msg) {
	case HSHELL_WINDOWCREATED:
	case HSHELL_REDRAW:
		printf("ShlProc: %p - %s\n", hwnd, msg == HSHELL_REDRAW ?
				"redraw" : "window created");
		if (!(win = win_find(hwnd,0)))
			if ((win = win_find(hwnd,1)))
				wm_insert(win);
		return 1;
	case HSHELL_WINDOWDESTROYED:
		printf("ShlProc: %p - window destroyed\n", hwnd);
		if ((win = win_find(hwnd,0)) &&
		    win->sys->state == st_show) {
			wm_remove(win);
			win_remove(win);
		}
		return 1;
	case HSHELL_WINDOWACTIVATED:
		printf("ShlProc: %p - window activated\n", hwnd);
		return 0;
	default:
		printf("ShlProc: %p - unknown msg, %d\n", hwnd, msg);
		return 0;
	}
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	//printf("WndProc: %d, %x, %lx\n", msg, wParam, lParam);
	switch (msg) {
	case WM_CREATE:  printf("WndProc: %p - create\n",  hwnd); return 0;
	case WM_CLOSE:   printf("WndProc: %p - close\n",   hwnd); return 0;
	case WM_DESTROY: printf("WndProc: %p - destroy\n", hwnd); return 0;
	case WM_HOTKEY:  printf("WndProc: %p - hotkey\n",  hwnd); return 0;
	}
	if (msg == shellhookid)
		if (ShlProc(wParam, lParam, 0))
			return 1;
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

BOOL CALLBACK MonProc(HMONITOR mon, HDC dc, LPRECT rect, LPARAM _screens)
{
	MONITORINFO info = {.cbSize=sizeof(MONITORINFO)};
	GetMonitorInfo(mon, &info);
	RECT *work = &info.rcWork;

	list_t **screens = (list_t**)_screens;
	win_t *screen = new0(win_t);
	screen->x = work->left;
	screen->y = work->top;
	screen->w = work->right  - work->left;
	screen->h = work->bottom - work->top;
	*screens = list_append(*screens, screen);
	printf("mon_proc: %d,%d %dx%d\n",
		screen->x, screen->y, screen->w, screen->h);
	return TRUE;
}

/********************
 * System functions *
 ********************/
void sys_move(win_t *win, int x, int y, int w, int h)
{
	printf("sys_move: %p - %d,%d  %dx%d\n", win, x, y, w, h);
	win->x = x; win->y = y;
	win->w = MAX(w,1); win->h = MAX(h,1);
	MoveWindow(win->sys->hwnd, win->x, win->y, win->w, win->h, TRUE);
}

void sys_raise(win_t *win)
{
	printf("sys_raise: %p\n", win);
	SetForegroundWindow(win->sys->hwnd);

	//HWND hwnd = win->sys->hwnd;
	//HWND top  = GetAncestor(hwnd,GA_ROOT);
	//SetWindowPos(top, HWND_TOPMOST, 0, 0, 0, 0,
	//		SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
}

void sys_focus(win_t *win)
{
	printf("sys_focus: %p\n", win);

	/* Windows prevents a thread from using SetForegroundInput under
	 * certain circumstances and instead flashes the windows toolbar icon.
	 * Attaching the thread input queues avoids this behavior */
	DWORD oldId = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
	DWORD newId = GetCurrentThreadId();
	AttachThreadInput(oldId, newId, TRUE);

	BringWindowToTop(win->sys->hwnd);
	SetForegroundWindow(win->sys->hwnd);
	SetFocus(win->sys->hwnd);

	AttachThreadInput(oldId, newId, FALSE);
}

void sys_show(win_t *win, state_t state)
{
	static struct {
		char *str;
		int   cmd;
	} map[] = {
		[st_show ] {"show" , SW_SHOW    },
		[st_full ] {"full" , SW_MAXIMIZE},
		[st_shade] {"shade", SW_SHOW    },
		[st_icon ] {"icon" , SW_MINIMIZE},
		[st_hide ] {"hide" , SW_HIDE    },
	};
	win->sys->state = state;
	printf("sys_show: %s\n", map[state].str);
	ShowWindow(win->sys->hwnd, map[state].cmd);
}

void sys_watch(win_t *win, Key_t key, mod_t mod)
{
	(void)key2w; // TODO
	//printf("sys_watch: %p\n", win);
}

void sys_unwatch(win_t *win, Key_t key, mod_t mod)
{
	(void)key2w; // TODO
	//printf("sys_unwatch: %p\n", win);
}

list_t *sys_info(win_t *win)
{
	list_t *screens = NULL;
	EnumDisplayMonitors(NULL, NULL, MonProc, (LPARAM)&screens);
	return screens;
}

win_t *sys_init(void)
{
	HINSTANCE hInst = GetModuleHandle(NULL);
	HWND      hwnd  = NULL;

	/* Setup window class */
	WNDCLASSEX wc    = {
		.cbSize        = sizeof(WNDCLASSEX),
		.lpfnWndProc   = WndProc,
		.hInstance     = hInst,
		.lpszClassName = "wmpus_class",
	};
	if (!RegisterClassEx(&wc))
		printf("sys_init: Error Registering Class - %lu\n", GetLastError());

	/* Get work area */
        RECT rc;
        SystemParametersInfo(SPI_GETWORKAREA, 0, &rc, 0);

	/* Create shell hook window */
	if (!(hwnd = CreateWindowEx(0, "wmpus_class", "wmpus", 0,
			rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top,
			HWND_MESSAGE, NULL, hInst, NULL)))
		printf("sys_init: Error Creating Shell Hook Window - %lu\n", GetLastError());

	/* Register shell hook */
	BOOL (*RegisterShellHookWindow)(HWND) = (void*)GetProcAddress(
			GetModuleHandle("USER32.DLL"), "RegisterShellHookWindow");
	if (!RegisterShellHookWindow)
		printf("sys_init: Error Finding RegisterShellHookWindow - %lu\n", GetLastError());
	if (!RegisterShellHookWindow(hwnd))
		printf("sys_init: Error Registering ShellHook Window - %lu\n", GetLastError());
	shellhookid = RegisterWindowMessage("SHELLHOOK");

	/* Input hooks */
	SetWindowsHookEx(WH_KEYBOARD_LL, KbdProc, hInst, 0);
	SetWindowsHookEx(WH_MOUSE_LL,    MllProc, hInst, 0);
	//SetWindowsHookEx(WH_SHELL,       ShlProc, hInst, 0);

	/* Alternate ways to get input */
	//if (!RegisterHotKey(hwnd, 123, MOD_CONTROL, VK_LBUTTON))
	//	printf("sys_init: Error Registering Hotkey - %lu\n", GetLastError());
	//if (!RegisterHotKey(NULL, 123, MOD_CONTROL, VK_LBUTTON))
	//	printf("sys_init: Error Registering Hotkey - %lu\n", GetLastError());

	return root = win_new(hwnd,0);
}

void sys_run(win_t *root)
{
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}
