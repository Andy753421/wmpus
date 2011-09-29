/*
 * Copyright (C) 2011 Andy Spencer <andy753421@gmail.com>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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

list_t *list_remove(list_t *head, list_t *item);

int list_length(list_t *item);

list_t *list_last(list_t *list);

list_t *list_find(list_t *list, void *data);

/* Misc */
int error(char *fmt, ...);
