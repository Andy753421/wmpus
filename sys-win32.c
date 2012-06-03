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
#include <ctype.h>
#include <search.h>

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <winbase.h>
#include <winuser.h>

#include "util.h"
#include "conf.h"
#include "sys.h"
#include "wm.h"

/* Configuration */
static int no_capture = 0;
static int stack      = 25;

/* Internal structures */
struct win_sys {
	HWND hwnd;
};

typedef struct {
	event_t ev;
	int     vk;
} event_map_t;

/* Global data */
static int     shellhookid;
static void   *cache;
static win_t  *root;
static list_t *screens;

/* Conversion functions */
static event_map_t ev2vk[] = {
	{EV_MOUSE1  , VK_LBUTTON },
	{EV_MOUSE2  , VK_MBUTTON },
	{EV_MOUSE3  , VK_RBUTTON },
	{EV_LEFT    , VK_LEFT    },
	{EV_RIGHT   , VK_RIGHT   },
	{EV_UP      , VK_UP      },
	{EV_DOWN    , VK_DOWN    },
	{EV_HOME    , VK_HOME    },
	{EV_END     , VK_END     },
	{EV_PAGEUP  , VK_PRIOR   },
	{EV_PAGEDOWN, VK_NEXT    },
	{EV_F1      , VK_F1      },
	{EV_F2      , VK_F2      },
	{EV_F3      , VK_F3      },
	{EV_F4      , VK_F4      },
	{EV_F5      , VK_F5      },
	{EV_F6      , VK_F6      },
	{EV_F7      , VK_F7      },
	{EV_F8      , VK_F8      },
	{EV_F9      , VK_F9      },
	{EV_F10     , VK_F10     },
	{EV_F11     , VK_F11     },
	{EV_F12     , VK_F12     },
	{EV_SHIFT   , VK_SHIFT   },
	{EV_SHIFT   , VK_LSHIFT  },
	{EV_SHIFT   , VK_RSHIFT  },
	{EV_CTRL    , VK_CONTROL },
	{EV_CTRL    , VK_LCONTROL},
	{EV_CTRL    , VK_RCONTROL},
	{EV_ALT     , VK_MENU    },
	{EV_ALT     , VK_LMENU   },
	{EV_ALT     , VK_RMENU   },
	{EV_WIN     , VK_LWIN    },
	{EV_WIN     , VK_RWIN    },
};

/* - Keycodes */
static event_t w2ev(UINT vk)
{
	event_map_t *em = map_getr(ev2vk,vk);
	return em ? em->ev : tolower(vk);
}

static UINT ev2w(event_t ev)
{
	event_map_t *em = map_get(ev2vk,ev);
	return em ? em->vk : toupper(ev);
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
	event_t ev = w2ev(st->vkCode);
	mod_t mod = getmod();
	mod.up = !!(st->flags & 0x80);
	printf("KbdProc: %d,%x,%lx - %lx,%lx,%lx - %x,%x\n",
			msg, wParam, lParam,
			st->vkCode, st->scanCode, st->flags,
			ev, mod2int(mod));
	return wm_handle_event(win_focused() ?: root, ev, mod, getptr())
		|| CallNextHookEx(0, msg, wParam, lParam);
}

LRESULT CALLBACK MllProc(int msg, WPARAM wParam, LPARAM lParam)
{
	event_t ev = EV_NONE;
	mod_t  mod = getmod();
	win_t *win = win_cursor();

	/* Update modifiers */
	switch (wParam) {
	case WM_LBUTTONDOWN: mod.up = 0; ev = EV_MOUSE1; break;
	case WM_LBUTTONUP:   mod.up = 1; ev = EV_MOUSE1; break;
	case WM_RBUTTONDOWN: mod.up = 0; ev = EV_MOUSE3; break;
	case WM_RBUTTONUP:   mod.up = 1; ev = EV_MOUSE3; break;
	}

	/* Check for focus-in/focus-out */
	static win_t *old = NULL;
	if (win && win != old) {
		wm_handle_event(old, EV_LEAVE, mod, getptr());
		wm_handle_event(win, EV_ENTER, mod, getptr());
	}
	old = win;

	/* Send mouse movement event */
	if (wParam == WM_MOUSEMOVE)
		return wm_handle_ptr(win_cursor(), getptr());
	else if (ev != EV_NONE)
		return wm_handle_event(win_cursor(), ev, mod, getptr());
	else
		return CallNextHookEx(0, msg, wParam, lParam);
}

LRESULT CALLBACK ShlProc(int msg, WPARAM wParam, LPARAM lParam)
{
	HWND hwnd = (HWND)wParam;
	win_t *win = NULL;
	switch (msg) {
	case HSHELL_REDRAW:
	case HSHELL_WINDOWCREATED:
		printf("ShlProc: %p - %s\n", hwnd, msg == HSHELL_REDRAW ?
				"redraw" : "window created");
		if (!(win = win_find(hwnd,0)))
			if ((win = win_find(hwnd,1)))
				wm_insert(win);
		return 1;
	case HSHELL_WINDOWREPLACED:
	case HSHELL_WINDOWDESTROYED:
		printf("ShlProc: %p - %s\n", hwnd, msg == HSHELL_WINDOWREPLACED ?
				"window replaced" : "window destroyed");
		if ((win = win_find(hwnd,0)) &&
		    (win->state == ST_SHOW ||
		     win->state == ST_SHADE)) {
			wm_remove(win);
			win_remove(win);
		}
		return 1;
	case HSHELL_WINDOWACTIVATED:
		printf("ShlProc: %p - window activated\n", hwnd);
		// Fake button-click (causes crazy switching)
		//if ((win = win_find(hwnd,0)))
		//	wm_handle_event(win, EV_MOUSE1, MOD(), getptr());
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
	screen->z = !!(info.dwFlags & MONITORINFOF_PRIMARY);
	screen->w = work->right  - work->left;
	screen->h = work->bottom - work->top;
	*screens = list_append(*screens, screen);
	printf("mon_proc: %d,%d %dx%d\n",
		screen->x, screen->y, screen->w, screen->h);
	return TRUE;
}

BOOL CALLBACK LoopProc(HWND hwnd, LPARAM user)
{
	win_t *win;
	if ((win = win_find(hwnd,1)))
		wm_insert(win);
	return TRUE;
}

BOOL WINAPI CtrlProc(DWORD type)
{
	sys_exit();
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

	/* See note in sys_focus */
	DWORD oldId = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
	DWORD newId = GetCurrentThreadId();
	AttachThreadInput(oldId, newId, TRUE);

	SetWindowPos(win->sys->hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE);

	AttachThreadInput(oldId, newId, FALSE);
}

void sys_focus(win_t *win)
{
	printf("sys_focus: %p\n", win);

	/* Windows prevents a thread from using SetForegroundInput under
	 * certain circumstances and instead flashes the windows toolbar icon.
	 * Attaching the thread input queues avoids this behavior */
	HWND  fgWin = GetForegroundWindow();
	if (fgWin == win->sys->hwnd)
		return; // already focused
	DWORD oldId = GetWindowThreadProcessId(fgWin, NULL);
	DWORD newId = GetCurrentThreadId();
	if (oldId != newId)
		AttachThreadInput(oldId, newId, TRUE);

	HWND next = GetWindow(win->sys->hwnd, GW_HWNDNEXT);
	SetWindowPos(win->sys->hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE);
	if (next)
		SetWindowPos(win->sys->hwnd, next, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE);

	if (oldId != newId)
		AttachThreadInput(oldId, newId, FALSE);
}

void sys_show(win_t *win, state_t state)
{
	static struct {
		char *str;
		int   cmd;
	} map[] = {
		[ST_SHOW ] {"show" , SW_SHOW    },
		[ST_FULL ] {"full" , SW_MAXIMIZE},
		[ST_SHADE] {"shade", SW_SHOW    },
		[ST_ICON ] {"icon" , SW_MINIMIZE},
		[ST_HIDE ] {"hide" , SW_HIDE    },
	};
	if (win->state != state && win->state == ST_SHADE)
		SetWindowRgn(win->sys->hwnd, NULL, TRUE);
	win->state = state;
	printf("sys_show: %s\n", map[state].str);
	ShowWindow(win->sys->hwnd, map[state].cmd);
	if (state == ST_SHADE)
		SetWindowRgn(win->sys->hwnd, CreateRectRgn(0,0,win->w,stack), TRUE);
}

void sys_watch(win_t *win, event_t ev, mod_t mod)
{
	(void)ev2w; // TODO
	//printf("sys_watch: %p\n", win);
}

void sys_unwatch(win_t *win, event_t ev, mod_t mod)
{
	(void)ev2w; // TODO
	//printf("sys_unwatch: %p\n", win);
}

list_t *sys_info(win_t *win)
{
	if (screens == NULL)
		EnumDisplayMonitors(NULL, NULL, MonProc, (LPARAM)&screens);
	return screens;
}

win_t *sys_init(void)
{
	HINSTANCE hInst = GetModuleHandle(NULL);
	HWND      hwnd  = NULL;

	/* Load configuration */
	no_capture = conf_get_int("main.no-capture", no_capture);
	stack      = conf_get_int("main.stack",      stack);

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

	/* Capture ctrl-c and console widnow close */
	SetConsoleCtrlHandler(CtrlProc, TRUE);

	return root = win_new(hwnd,0);
}

void sys_run(win_t *root)
{
	MSG msg = {};
	if (!no_capture)
		EnumWindows(LoopProc, 0);
	while (GetMessage(&msg, NULL, 0, 0) > 0 &&
	       msg.message != WM_QUIT) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

void sys_exit(void)
{
	PostMessage(root->sys->hwnd, WM_QUIT, 0, 0);
}

void sys_free(win_t *root)
{
	/* I don't really care about this
	 * since I don't know how to use
	 * valgrind on win32 anyway.. */
}
