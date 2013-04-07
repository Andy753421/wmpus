/*
 * Copyright (c) 2013 Andy Spencer <andy753421@gmail.com>
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

/* Brain-dead monocule window manager */

#include <stdio.h>

#include "util.h"
#include "sys.h"
#include "wm.h"

/* Configuration */
#ifndef MODKEY
#define MODKEY alt
#endif

/* Data */
list_t *wins;
list_t *focus;
list_t *screens;

/* Helper functions */
void wm_show(list_t *node)
{
	focus = node;
	if (!node) return;
	sys_show(node->data, ST_MAX);
	sys_raise(node->data);
	sys_focus(node->data);
}

/* Window management functions */
int wm_handle_event(win_t *win, event_t ev, mod_t mod, ptr_t ptr)
{
	list_t *node = list_find(wins, win);

	if (node && mod.MODKEY && ev == 'j')
		return wm_show(node->next), 1;
	if (node && mod.MODKEY && ev == 'k')
		return wm_show(node->prev), 1;

	if (mod.MODKEY && mod.shift && ev == 'c')
		return sys_show(win, ST_CLOSE), 1;
	if (mod.MODKEY && mod.shift && ev == 'q')
		return sys_exit(), 1;

	return 0;
}

int wm_handle_ptr(win_t *cwin, ptr_t ptr)
{
	return 0;
}

int wm_handle_state(win_t *win, state_t prev, state_t next)
{
	return 0;
}

void wm_insert(win_t *win)
{
	if (win->type == TYPE_NORMAL) {
		wins = list_append(wins, win);
		wm_show(list_last(wins));
	}
	if (win->type == TYPE_TOOLBAR) {
		wm_show(focus);
	}
}

void wm_remove(win_t *win)
{
	list_t *node = list_find(wins, win);
	if (node == focus)
		wm_show(node->prev ?: node->next);
	wins = list_remove(wins, node, 0);
}

void wm_init(win_t *root)
{
	screens = sys_info(root);
	sys_watch(root, 'j', MOD(.MODKEY=1));
	sys_watch(root, 'k', MOD(.MODKEY=1));
	sys_watch(root, 'c', MOD(.MODKEY=1,.shift=1));
	sys_watch(root, 'q', MOD(.MODKEY=1,.shift=1));
}

void wm_free(win_t *root)
{
	while (wins)
		wins = list_remove(wins, wins, 0);
}
