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

/* Various utility functions */

/* Misc macros */
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))

#define new0(type) (calloc(1, sizeof(type)))

#define countof(x) (sizeof(x)/sizeof((x)[0]))

/* Constant length map functions */
#define map_get(map, k, kv, v, def) ({         \
	typeof(def) val = def;                 \
	for (int i = 0; i < countof(map); i++) \
		if (map[i].k == kv) {          \
			val = map[i].v;        \
			break;                 \
		}                              \
	val;                                   \
})

/* Linked lists */
typedef struct list {
	struct list *prev;
	struct list *next;
	void   *data;
} list_t;

list_t *list_insert(list_t *after, void *data);

void list_insert_after(list_t *after, void *data);

list_t *list_append(list_t *before, void *data);

list_t *list_remove(list_t *head, list_t *item, int freedata);

int list_length(list_t *item);

list_t *list_last(list_t *list);

list_t *list_find(list_t *list, void *data);

list_t *list_sort(list_t *list, int rev, int (*func)(void*,void*));

/* Misc */
int residual(float num, float *state);

int str2num(char *str, int def);

int warn(char *fmt, ...);

int error(char *fmt, ...);
