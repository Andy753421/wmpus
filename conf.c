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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <search.h>

#include "util.h"
#include "conf.h"

/* Types */
typedef enum { number, string } type_t;

typedef struct {
	type_t  type;
	char   *key;
	union {
		int   num;
		char *str;
	};
} entry_t;

/* Data */
static void *conf;

/* Helpers */
static int conf_cmp(const void *_a, const void *_b)
{
	const entry_t *a = _a;
	const entry_t *b = _b;
	return strcmp(a->key, b->key);
}

static entry_t *conf_get(const char *key)
{
	entry_t try = { .key = (char*)key };
	entry_t **found = tfind(&try, &conf, conf_cmp);
	return found ? *found : NULL;
}

static void conf_set(const char *key, int num, const char *str)
{
	entry_t *entry;

	/* Free old item */
	if ((entry = conf_get(key))) {
		tdelete(entry, &conf, conf_cmp);
		free(entry->key);
		if (entry->type == string)
			free(entry->str);
		free(entry);
	}

	/* Insert new item */
	entry = new0(entry_t);
	entry->key = strdup(key);
	if (str) {
		entry->type = string;
		entry->str  = strdup(str);
	} else {
		entry->type = number;
		entry->num  = num;
	}
	tsearch(entry, &conf, conf_cmp);
}

static char *strtrim(char *str)
{
	while (*str == ' ' || *str == '\t' || *str == '\n')
		str++;
	char *end = strchr(str, '\0');
	while (end > str && (*end == ' '  || *end == '\t' ||
	                     *end == '\n' || *end == '\0'))
		*end-- = '\0';
	return str;
}

/* Read an ini file into the configuration
 *  - this does not properly handle excape sequences
 *  - could probably make this into a real parser.. */
static void load_file(const char *path)
{
	char line[256]={}, section[256]={};
	char key[256]={}, val[256]={};
	FILE *fd = fopen(path, "rt");
	if (!fd) return;
	printf("load_file: %s\n", path);
	while (fgets(line, sizeof(line), fd)) {
		/* Find special characters */
		char *lbrace = strchr(           line   , '[');
		char *rbrace = strchr((lbrace ?: line)+1, ']');
		char *equal  = strchr(           line   , '=');
		char *lquote = strchr((equal  ?: line)+1, '"');
		char *rquote = strchr((lquote ?: line)+1, '"');
		char *hash   = strchr((rquote ?: line)+1, '#');
		if (hash)
			*hash = '\0';

		if (lbrace && rbrace) {
			/* Check for secions */
			memcpy(section, lbrace+1, rbrace-lbrace-1);
		}
		else if (section[0] && equal && lquote && rquote) {
			/* Check for numbers/plain strings */
			memcpy(key, line, equal-line);
			memcpy(val, lquote+1, rquote-lquote-1);
			char *_key = strtrim(key);
			conf_set_str(_key, val);
			printf("  [%s.%s] = [%s]\n", section, _key, val);
		}
		else if (section[0] && equal) {
			/* Check for strings */
			memcpy(key, line, equal-line);
			strcpy(val, equal+1);
			char *_key = strtrim(key);
			char *_val = strtrim(val);
			char *end;
			int num = strtol(_val, &end, 10);
			if (end != val && *end == '\0')
				conf_set_int(_key, num);
			else if (!strcasecmp(_val, "true"))
				conf_set_int(_key, 1);
			else if (!strcasecmp(_val, "false"))
				conf_set_int(_key, 0);
			else
				conf_set_str(_key, _val);
			printf("  [%s.%s] = [%s]\n", section, _key, _val);
		}
	}
	fclose(fd);
}

static void load_args(int argc, char **argv)
{
	/* ... */
}


/* Configuration file functions */
int conf_get_int(const char *key)
{
	entry_t *entry = conf_get(key);
	return entry ? entry->num : 0;
}

void conf_set_int(const char *key, int value)
{
	conf_set(key, value, NULL);
}

const char *conf_get_str(const char *key)
{
	entry_t *entry = conf_get(key);
	return entry ? entry->str : NULL;
}

void conf_set_str(const char *key, const char *value)
{
	conf_set(key, 0, value);
}

void conf_init(int argc, char **argv)
{
	/* Load configuration */
	char path[256];
	snprintf(path, sizeof(path), "%s/%s", getenv("HOME"), ".wmpus");
	load_file(path);
	load_args(argc, argv);
}

void conf_free(void)
{
}
