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

/* Brain-dead multiple desktop manager */

#include <stdio.h>

#include "util.h"
#include "types.h"
#include "sys.h"
#include "wm.h"

/* Configuration */
#ifndef MODKEY
#define MODKEY alt
#endif

/* Data */
int     tag = 1;
list_t *tags[10];

/* Window management functions */
void wm_update(void)
{
}

int wm_handle_event(win_t *win, event_t ev, mod_t mod, ptr_t ptr)
{
	int new = ev - '0';
	if (!win || !mod.MODKEY || mod.up || new == tag ||
			ev < '0' || ev > '9')
		return 0;

	if (mod.shift) {
		list_t *node = list_find(tags[tag], win);
		if (node == NULL)
			return 0;
		tags[tag] = list_remove(tags[tag], node, 0);
		tags[new] = list_insert(tags[new], win);
		sys_show(win, ST_HIDE);
	} else {
		for (list_t *cur = tags[new]; cur; cur = cur->next)
			sys_show(cur->data, ST_SHOW);
		for (list_t *cur = tags[tag]; cur; cur = cur->next)
			sys_show(cur->data, ST_HIDE);
		tag = new;
	}
	return 1;
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
	tags[tag] = list_insert(tags[tag], win);
}

void wm_remove(win_t *win)
{
	for (int i = 0; i < 10; i++) {
		list_t *node = list_find(tags[i], win);
		if (node == NULL)
			continue;
		tags[i] = list_remove(tags[i], node, 0);
	}
}

void wm_init(void)
{
	event_t keys[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
	for (int i = 0; i < countof(keys); i++) {
		sys_watch(NULL, keys[i], MOD(.MODKEY=1));
		sys_watch(NULL, keys[i], MOD(.MODKEY=1,.shift=1));
	}
}

void wm_free(void)
{
	for (int i = 0; i < 10; i++) {
		while (tags[i]) {
			sys_show(tags[i]->data, ST_SHOW);
			tags[i] = list_remove(tags[i], tags[i], 0);
		}
	}
}
