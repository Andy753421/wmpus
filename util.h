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
#define map_getg(map, test) ({ \
	int i; \
	for (i = 0; i < countof(map) && !(test); i++); \
	i < countof(map) ? &map[i] : NULL ; \
})

#define map_get(m,k)    map_getg(m,k==*((typeof(k)*)&m[i]))
#define map_getr(m,k)   map_getg(m,k==*(((typeof(k)*)&m[i+1])-1))
#define map_getk(m,k,a) map_getg(m,k==m[i].a)

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

/* Misc */
int str2num(char *str, int def);

int error(char *fmt, ...);
