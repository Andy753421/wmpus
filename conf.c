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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <search.h>
#include <getopt.h>

#include "util.h"
#include "conf.h"

/* Types */
typedef struct {
	enum {
		NUMBER,
		STRING,
	} type;
	char   *key;
	union {
		int   num;
		char *str;
	};
} entry_t;

/* Data */
static void  *conf;
static int    conf_argc;
static char **conf_argv;
static char   conf_path[256];

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
		if (entry->type == STRING)
			free(entry->str);
		free(entry);
	}

	/* Insert new item */
	entry = new0(entry_t);
	entry->key = strdup(key);
	if (str) {
		entry->type = STRING;
		entry->str  = strdup(str);
		//printf("set_str: %s = %s\n", key, str);
	} else {
		entry->type = NUMBER;
		entry->num  = num;
		//printf("set_num: %s = %d\n", key, num);
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
	char key[256]={}, val[256]={}, fullkey[256]={};
	FILE *fd = fopen(path, "rt");
	if (!fd) return;
	//printf("load_file: %s\n", path);
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
			snprintf(fullkey, sizeof(key), "%s.%s",
					section, strtrim(key));
			if (!strchr(fullkey, ' ')) {
				conf_set_str(fullkey, val);
				//printf("  [%s] = [%s]\n", fullkey, val);
			}
		}
		else if (section[0] && equal) {
			/* Check for strings */
			memcpy(key, line, equal-line);
			strcpy(val, equal+1);
			snprintf(fullkey, sizeof(key), "%s.%s",
					section, strtrim(key));
			if (!strchr(fullkey, ' ')) {
				char *end, *trim = strtrim(val);
				int num = strtol(trim, &end, 10);
				if (end != val && *end == '\0')
					conf_set_int(fullkey, num);
				else if (!strcasecmp(trim, "true"))
					conf_set_int(fullkey, 1);
				else if (!strcasecmp(trim, "false"))
					conf_set_int(fullkey, 0);
				else
					conf_set_str(fullkey, trim);
				//printf("  [%s] = [%s]\n", fullkey, trim);
			}
		}
	}
	fclose(fd);
}

/* Load config from command line options */
static struct option long_options[] = {
	/* name hasarg flag val */
	{"no-capture", 0, NULL, 'n'},
	{"border",     2, NULL, 'b'},
	{"margin",     2, NULL, 'm'},
	{"int",        1, NULL, 'i'},
	{"str",        1, NULL, 's'},
	{"help",       0, NULL, 'h'},
	{NULL,         0, NULL,  0 },
};

static void usage(int argc, char **argv)
{
	printf("Usage:\n");
	printf("  %s [OPTION...]\n", argv[0]);
	printf("\n");
	printf("Options:\n");
	printf("  -n, --no-capture   Do not arrange pre existing windows\n");
	printf("  -b, --border=n     Draw an n pixel window border\n");
	printf("  -m, --margin=n     Leave an n pixel margin around windows\n");
	printf("  -i, --int=key=num  Set integer config option\n");
	printf("  -s, --str=key=str  Set string config option\n");
	printf("  -h, --help         Print usage information\n");
}

static void load_args(int argc, char **argv)
{
	char *key, *val;
	while (1) {
		int c = getopt_long(argc, argv, "nb:m:i:s:h", long_options, NULL);
		if (c == -1)
			break;
		switch (c) {
		case 'n':
			conf_set_int("main.no-capture", 1);
			break;
		case 'b':
			conf_set_int("main.border", str2num(optarg, 2));
			break;
		case 'm':
			conf_set_int("main.margin", str2num(optarg, 15));
			break;
		case 'i':
		case 's':
			key = strdup(optarg);
			val = strchr(key, '=');
			if (val) {
				*val++ = '\0';
				if (c == 's')
					conf_set_str(key, val);
				else
					conf_set_int(key, atol(val));
			}
			free(key);
			break;
		case 'h':
			usage(argc, argv);
			exit(0);
		default:
			usage(argc, argv);
			exit(-1);
		}
	}
}


/* Configuration file functions */
int conf_get_int(const char *key, int def)
{
	entry_t *entry = conf_get(key);
	return entry && entry->type == NUMBER
		? entry->num : def;
}

void conf_set_int(const char *key, int value)
{
	conf_set(key, value, NULL);
}

const char *conf_get_str(const char *key, const char *def)
{
	entry_t *entry = conf_get(key);
	return entry && entry->type == STRING
		? entry->str : def;
}

void conf_set_str(const char *key, const char *value)
{
	conf_set(key, 0, value);
}

void conf_reload(void)
{
	load_file(conf_path);
	load_args(conf_argc, conf_argv);
}

void conf_init(int argc, char **argv)
{
	conf_argc = argc;
	conf_argv = argv;
	snprintf(conf_path, sizeof(conf_path), "%s/%s",
			getenv("HOME") ?: getenv("HOMEPATH") ?: ".",
			".wmpus");
	conf_reload();
}

void conf_free(void)
{
}
