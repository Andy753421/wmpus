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
#include <signal.h>

#include "util.h"
#include "conf.h"
#include "types.h"
#include "sys.h"
#include "wm.h"

void on_sigint(int signum)
{
	sys_exit();
}

int main(int argc, char **argv)
{
	setbuf(stdout, NULL); // debug
	signal(SIGINT, on_sigint);

	conf_init(argc, argv);
	sys_init();
	wm_init();

	sys_run();

	wm_free();
	sys_free();
	return 0;
}
