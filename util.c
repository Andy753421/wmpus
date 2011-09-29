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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "util.h"

/* Doubly linked lists */
list_t *list_insert(list_t *next, void *data)
{
	list_t *node = new0(list_t);
	node->data = data;
	node->next = next;
	node->prev = next ? next->prev : NULL;
	if (node->next) node->next->prev = node;
	if (node->prev) node->prev->next = node;
	return node;
}

void list_insert_after(list_t *prev, void *data)
{
	// prev must be valid,
	// as we cannot return the original list head
	list_t *node = new0(list_t);
	node->data = data;
	node->prev = prev;
	node->next = prev->next;
	prev->next = node;
	if (node->next) node->next->prev = node;
}

list_t *list_append(list_t *head, void *data)
{
	list_t *last = head;
	while (last && last->next)
		last = last->next;
	list_t *node = new0(list_t);
	node->data = data;
	node->prev = last;
	if (last) last->next = node;
	return last ? head : node;
}

list_t *list_remove(list_t *head, list_t *node)
{
	list_t *next = node->next;
	list_t *prev = node->prev;
	if (next) next->prev = prev;
	if (prev) prev->next = next;
	free(node);
	return head == node ? next : head;
}

int list_length(list_t *node)
{
	int len = 0;
	for (; node; node = node->next)
		len++;
	return len;
}

list_t *list_last(list_t *list)
{
	while (list && list->next)
		list = list->next;
	return list;
}

list_t *list_find(list_t *list, void *data)
{
	for (list_t *cur = list; cur; cur = cur->next)
		if (cur->data == data)
			return cur;
	return NULL;
}

/* Misc */
int error(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "Error: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(1);
	return 0;
}
