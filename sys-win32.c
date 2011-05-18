#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0500
#include <windows.h>
#include <winbase.h>
#include <winuser.h>

#include "util.h"
#include "sys.h"
#include "wm.h"

int   test;
int   shellhookid;
mod_t mod_state;

/* Internal structures */
struct win_sys {
	HWND hwnd;
};

typedef struct {
	Key_t key;
	int   vk;
} keymap_t;

win_t *win_new(HWND hwnd)
{
	RECT rect = {};
	GetWindowRect(hwnd, &rect);
	win_t *win = new0(win_t);
	win->x         = rect.left;
	win->y         = rect.top;
	win->w         = rect.right  - rect.left;
	win->h         = rect.bottom - rect.top;
	win->sys       = new0(win_sys_t);
	win->sys->hwnd = hwnd;
	return win;
}

/* Conversion functions */
keymap_t key2vk[] = {
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

UINT key2w(Key_t key)
{
	keymap_t *km = map_get(key2vk,key);
	return km ? km->vk : toupper(key);
}
Key_t w2key(UINT vk)
{
	keymap_t *km = map_getr(key2vk,vk);
	return km ? km->key : vk;
}

ptr_t getptr(void)
{
	POINT wptr;
	GetCursorPos(&wptr);
	return (ptr_t){-1, -1, wptr.x, wptr.y};
}

/* Functions */
void sys_move(win_t *win, int x, int y, int w, int h)
{
	printf("sys_move: %p - %d,%d  %dx%d\n", win, x, y, w, h);
	MoveWindow(win->sys->hwnd, x, y, w, h, TRUE);
}

void sys_raise(win_t *win)
{
	printf("sys_raise: %p\n", win);
}

void sys_watch(win_t *win, Key_t key, mod_t mod)
{
	printf("sys_watch: %p\n", win);
}

LRESULT CALLBACK KbdProc(int msg, WPARAM wParam, LPARAM lParam)
{
	KBDLLHOOKSTRUCT *st = (KBDLLHOOKSTRUCT *)lParam;
	Key_t key = w2key(st->vkCode);
	mod_state.up = !!(st->flags & 0x80);
	if (key == key_alt  ) mod_state.alt   = !mod_state.up;
	if (key == key_ctrl ) mod_state.ctrl  = !mod_state.up;
	if (key == key_shift) mod_state.shift = !mod_state.up;
	if (key == key_win  ) mod_state.win   = !mod_state.up;
	printf("KbdProc: %d,%x,%lx - %lx,%lx,%lx - %x,%x\n",
			msg, wParam, lParam,
			st->vkCode, st->scanCode, st->flags,
			key, mod2int(mod_state));
	HWND fghwnd = GetForegroundWindow();
	wm_handle_key(win_new(fghwnd), key, mod_state, getptr());
	return CallNextHookEx(0, msg, wParam, lParam);
}

LRESULT CALLBACK MllProc(int msg, WPARAM wParam, LPARAM lParam)
{
	Key_t key   = key_none;
	HWND fghwnd = GetForegroundWindow();
	switch (wParam) {
	case WM_LBUTTONDOWN: mod_state.up = 0; key = key_mouse1; break;
	case WM_LBUTTONUP:   mod_state.up = 1; key = key_mouse1; break;
	case WM_RBUTTONDOWN: mod_state.up = 0; key = key_mouse3; break;
	case WM_RBUTTONUP:   mod_state.up = 1; key = key_mouse3; break;
	}
	if (wParam == WM_MOUSEMOVE)
		return wm_handle_ptr(win_new(fghwnd), getptr());
	else if (key != key_none)
		return wm_handle_key(win_new(fghwnd), key, mod_state, getptr());
	else
		return CallNextHookEx(0, msg, wParam, lParam);
}

LRESULT CALLBACK ShlWndProc(HWND hwnd, int msg, WPARAM wParam, LPARAM lParam)
{
	printf("ShlWndProc: %d, %x, %lx\n", msg, wParam, lParam);
	switch (wParam) {
	case HSHELL_WINDOWCREATED:
		printf("ShlProc: window created\n");
		return 0;
	case HSHELL_WINDOWDESTROYED:
		printf("ShlProc: window destroyed\n");
		return 0;
	case HSHELL_WINDOWACTIVATED:
		printf("ShlProc: window activated\n");
		return 0;
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	printf("WndProc: %d, %x, %lx\n", msg, wParam, lParam);
	switch (msg) {
	case WM_CREATE:
	case WM_CLOSE:
	case WM_DESTROY:
		return 0;
	}
	if (msg == shellhookid) {
		printf("WndProc: shellhook\n");
		return ShlWndProc(hwnd, msg, wParam, lParam);
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

win_t *sys_init(void)
{
	HINSTANCE hInst = GetModuleHandle(NULL);
	test = 123;

	/* Class */
	WNDCLASSEX wc    = {};
	wc.cbSize        = sizeof(WNDCLASSEX);
	wc.lpfnWndProc   = WndProc;
	wc.hInstance     = hInst;
	wc.lpszClassName = "awm_class";
	if (!RegisterClassEx(&wc))
		printf("sys_init: Error Registering Class - %lu\n", GetLastError());

	/* Bargh!? */
	HWND hwnd = CreateWindowEx(0, "awm_class", "awm", 0,
			0, 0, 0, 0, HWND_MESSAGE, NULL, hInst, NULL);
	if (!hwnd)
		printf("sys_init: Error Creating Window - %lu\n", GetLastError());

	/* Try Shell Hook Window */
	HINSTANCE hInstUser32 = GetModuleHandle("USER32.DLL");
	BOOL (*RegisterShellHookWindow)(HWND hwnd) = (void*)GetProcAddress(hInstUser32, "RegisterShellHookWindow");
	if (!RegisterShellHookWindow)
		printf("sys_init: Error Finding RegisterShellHookWindow - %lu\n", GetLastError());
	if (!RegisterShellHookWindow(hwnd))
		printf("sys_init: Error Registering ShellHook Window - %lu\n", GetLastError());
	shellhookid = RegisterWindowMessage("SHELLHOOK");

	/* Register other hooks for testing */
	SetWindowsHookEx(WH_MOUSE_LL,    MllProc, hInst, 0);
	SetWindowsHookEx(WH_KEYBOARD_LL, KbdProc, hInst, 0);
	//SetWindowsHookEx(WH_SHELL,       ShlProc, hInst, 0);

	//if (!RegisterHotKey(hwnd, 123, MOD_CONTROL, VK_LBUTTON))
	//	printf("sys_init: Error Registering Hotkey - %lu\n", GetLastError());
	if (!RegisterHotKey(NULL, 123, MOD_CONTROL, VK_LBUTTON))
		printf("sys_init: Error Registering Hotkey - %lu\n", GetLastError());

	return win_new(hwnd);

}

void sys_run(win_t *root)
{
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}
