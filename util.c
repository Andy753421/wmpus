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

list_t *list_remove(list_t *head, list_t *node, int freedata)
{
	list_t *next = node->next;
	list_t *prev = node->prev;
	if (next) next->prev = prev;
	if (prev) prev->next = next;
	if (freedata)
		free(node->data);
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

list_t *list_sort(list_t *list, int rev, int (*func)(void *a, void*b))
{
	if (list == NULL || list->next == NULL)
		return list;

	/* Split list */
	list_t *sides[2] = {NULL, NULL};
	for (int i = 0; list; i=(i+1)%2) {
		list_t *head = list;
		list = list->next;
		head->next = sides[i];
		sides[i]   = head;
	}

	/* Sort sides */
	sides[0] = list_sort(sides[0], !rev, func);
	sides[1] = list_sort(sides[1], !rev, func);

	/* Merge sides */
	while (sides[0] || sides[1]) {
		int i = sides[0] == NULL ? 1 :
		        sides[1] == NULL ? 0 :
			func(sides[0]->data,
			     sides[1]->data) > 0 ? !!rev : !rev;
		list_t *head = sides[i];
		sides[i] = sides[i]->next;
		head->next = list;
		head->prev = NULL;
		if (list)
			list->prev = head;
		list = head;
	}
	return list;
}

/* Misc */
int str2num(char *str, int def)
{
	char *end = NULL;
	int num = strtol(str, &end, 10);
	return end && *end == '\0' ? num : def;
}

int warn(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "Warning: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	return 0;
}

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
