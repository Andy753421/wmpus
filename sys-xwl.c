/*
 * Copyright (c) 2014-2015 Andy Spencer <andy753421@gmail.com>
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

#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>

#include <libevdev/libevdev.h>
#include <xkbcommon/xkbcommon.h>
#include <wayland-server.h>
#include <wayland-client.h>

#include <gtk/gtk.h>

#include "util.h"
#include "conf.h"
#include "sys.h"
#include "wm.h"

#include "xdg-shell-client-protocol.h"
#include "xdg-shell-server-protocol.h"
#include "gtk-shell-client-protocol.h"
#include "gtk-shell-server-protocol.h"

/* Window Managers calls */
void wm_update(void);

/* Wayland user data */
typedef struct {
	uint8_t             *mem;
	size_t               size;
} sys_pool_t;

typedef struct {
	sys_pool_t          *pool;
	uint8_t             *mem;
	cairo_surface_t     *surface;
} sys_bdata_t;

typedef struct {
	struct wl_client    *cli;
	list_t              *ptrs;    // of struct wl_resource
	list_t              *kbds;    // of struct wl_resource
	list_t              *tchs;    // of struct wl_resource
} sys_cdata_t;

/* Internal structures */
struct win_sys {
	struct wl_resource  *sfc;
	struct wl_resource  *ssfc;
	struct wl_resource  *xsfc;
	struct wl_resource  *buf;
	sys_cdata_t         *cdata;
	int                  x,y;     // surface x,y
	int                  wx,wy;   // window  x,y inside sfc
	int                  ww,wh;   // window  w,h inside sfc
};

/* Global data */
static win_t                *root;    // root window
static win_t                *hover;   // window the mouse is over
static win_t                *focus;   // focused window
static win_t                *cursor;  // mouse cursor image

static list_t               *clients; // of sys_cdata_t
static list_t               *windows; // of win_t

static double                cursor_x;
static double                cursor_y;
static double                cursor_dx;
static double                cursor_dy;

static GtkWidget            *screen;

static struct wl_resource   *output;
static struct wl_display    *display;
static struct wl_event_loop *events;

static struct wl_array       keys;

/* Constant data */
static struct wl_array       xstate_normal;
static struct wl_array       xstate_active;
static struct wl_array       xstate_max;
static struct wl_array       xstate_full;
static struct wl_array       xstate_resize;

/********************
 * Helper Functions *
 ********************/

static void set_array(struct wl_array *array, int n, ...)
{
	va_list ap;
	va_start(ap, n);
	wl_array_init(array);
	for (int i = 0; i < n; i++) {
		uint32_t item = va_arg(ap, uint32_t);
		uint32_t *ptr = wl_array_add(array, sizeof(item));
		if (!ptr)
			error("Unable to allocate constant");
		*ptr = item;
	}
	va_end(ap);
}

static int get_serial(void)
{
	static int serial = 0;
	return serial++;
}

static int get_time(void)
{
	static uint64_t epoch;

	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	uint64_t now = (((uint64_t)ts.tv_sec ) * 1000)
	             + (((uint64_t)ts.tv_nsec) / 1000000);

	if (epoch == 0)
		epoch = now;

	return (int)(now - epoch);
}

static ptr_t get_ptr(win_t *win)
{
	ptr_t ptr = {
		.rx = cursor_x,
		.ry = cursor_y,
	};
	if (win) {
		ptr.x = cursor_x - win->x;
		ptr.y = cursor_y - win->y;
	};
	return ptr;
}

static mod_t get_mod(unsigned int state, int up)
{
	return (mod_t){
	       .alt   = !!(state & GDK_MOD1_MASK   ),
	       .ctrl  = !!(state & GDK_CONTROL_MASK),
	       .shift = !!(state & GDK_SHIFT_MASK  ),
	       .win   = !!(state & GDK_MOD4_MASK   ),
	       .up    = up,
	};
}

static win_t *find_win(int x, int y)
{
	for (list_t *cur = windows; cur; cur = cur->next) {
		win_t *win = cur->data;
		printf("find_win -- %p -- %4d,%-4d : %4dx%-4d\n",
			win, win->x, win->y, win->w, win->h);
	}
	for (list_t *cur = windows; cur; cur = cur->next) {
		win_t *win = cur->data;
		int l = win->x;
		int r = win->w + l;
		int t = win->y;
		int b = win->h + t;
		if (l <= x && x <= r && t <= y && y <= b) {
			printf("find_win -> %p\n", win);
			return win;
		}
	}
	return NULL;
}

static sys_cdata_t *find_cdata(struct wl_client *cli)
{
	// Search for existing client
	for (list_t *cur = clients; cur; cur = cur->next) {
		sys_cdata_t *cdata = cur->data;
		if (cdata->cli == cli)
			return cdata;
	}

	// Not found, create new one
	sys_cdata_t *cdata = new0(sys_cdata_t);
	clients = list_insert(clients, cdata);
	cdata->cli = cli;
	return cdata;
}

/****************************
 * Wayland Buffer Interface *
 ****************************/
static void buffer_destroy(struct wl_client *cli, struct wl_resource *reso)
{
	printf("buffer_destroy\n");
}

static struct wl_buffer_interface buffer_iface = {
        .destroy  = buffer_destroy,
};

/***********************************
 * Wayland Shared Memory Interface *
 ***********************************/

/* Shm Pool */
static void pool_create_buffer(struct wl_client *cli, struct wl_resource *pool,
		uint32_t id, int32_t offset, int32_t width, int32_t height,
		int32_t stride, uint32_t format)
{
	sys_bdata_t *bdata = new0(sys_bdata_t);
	bdata->pool = wl_resource_get_user_data(pool);
	bdata->mem  = bdata->pool->mem + offset;

	printf("pool_create_buffer - %dx%d %p+%d : %d,%d,%d\n",
			width, height, bdata->pool->mem, offset, id, stride, format);

	if (offset > bdata->pool->size || offset < 0)
	{
		printf("\n\nerror\n\n");
		wl_resource_post_error(pool, WL_SHM_ERROR_INVALID_STRIDE,
				"offset is too big or negative");
		return;
	}

	cairo_format_t cf =
		format == WL_SHM_FORMAT_ARGB8888 ? CAIRO_FORMAT_ARGB32 :
		format == WL_SHM_FORMAT_XRGB8888 ? CAIRO_FORMAT_RGB24  : CAIRO_FORMAT_INVALID;

	bdata->surface = cairo_image_surface_create_for_data(bdata->mem, cf, width, height, stride);

	struct wl_resource *res = wl_resource_create(cli, &wl_buffer_interface,
                                        wl_resource_get_version(pool), id);
	wl_resource_set_implementation(res, &buffer_iface, bdata, NULL);
}

static void pool_destroy(struct wl_client *cli, struct wl_resource *pool)
{
	printf("pool_destroy\n");
}

static void pool_resize(struct wl_client *cli, struct wl_resource *pool,
		int32_t size)
{
	printf("[   WMPUS   ] pool_resize - %d\n", size);
	sys_pool_t *data = wl_resource_get_user_data(pool);
	void *ptr = mremap(data->mem, data->size, size, MREMAP_MAYMOVE);
	if (ptr == MAP_FAILED)
	{
		printf("\n\nerror\n\n");
		wl_resource_post_error(pool, WL_SHM_ERROR_INVALID_FD,
				"mremap failed: %s", strerror(errno));
		return;
	}
	data->mem  = ptr;
	data->size = size;
}

static struct wl_shm_pool_interface pool_iface = {
	.create_buffer = &pool_create_buffer,
	.destroy       = &pool_destroy,
	.resize        = &pool_resize,
};

/* Shm */
static void shm_create_pool(struct wl_client *cli, struct wl_resource *shm,
		uint32_t id, int32_t fd, int32_t size)
{
	printf("shm_create_pool - #%d %d %d\n", id, fd, size);

	sys_pool_t *data = new0(sys_pool_t);

	struct wl_resource *res = wl_resource_create(cli, &wl_shm_pool_interface,
                                        wl_resource_get_version(shm), id);
	wl_resource_set_implementation(res, &pool_iface, data, NULL);

	data->mem  = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	data->size = size;
}

static struct wl_shm_interface shm_iface = {
	.create_pool = shm_create_pool,
};

/**************************
 * Wayland Seat Interface *
 **************************/

/* Pointer */
static void pointer_kill(struct wl_resource *ptr)
{
	sys_cdata_t *cdata = wl_resource_get_user_data(ptr);
	list_t *link = list_find(cdata->ptrs, ptr);
	cdata->ptrs = list_remove(cdata->ptrs, link, 0);
	if (!cdata->ptrs && !cdata->kbds && !cdata->tchs) {
		list_t *clink = list_find(clients, cdata);
		clients = list_remove(clients, clink, 1);
	}
}

static void pointer_set_cursor(struct wl_client *cli, struct wl_resource *ptr,
			   uint32_t serial, struct wl_resource *sfc,
			   int32_t hotspot_x, int32_t hotspot_y)
{
	printf("pointer_set_cursor %d,%d\n", hotspot_x, hotspot_y);
	win_t *win = wl_resource_get_user_data(sfc);
	win->type = TYPE_CURSOR;
	cursor_dx = hotspot_x;
	cursor_dy = hotspot_y;
	cursor    = win;
}

static void pointer_release(struct wl_client *cli, struct wl_resource *ptr)
{
	printf("pointer_release\n");
}

static struct wl_pointer_interface pointer_iface = {
        .set_cursor = pointer_set_cursor,
        .release    = pointer_release,
};

/* Keyboard */
static void keyboard_kill(struct wl_resource *kbd)
{
	sys_cdata_t *cdata = wl_resource_get_user_data(kbd);
	list_t *link = list_find(cdata->kbds, kbd);
	cdata->kbds = list_remove(cdata->kbds, link, 0);
	if (!cdata->ptrs && !cdata->kbds && !cdata->tchs) {
		list_t *clink = list_find(clients, cdata);
		clients = list_remove(clients, clink, 1);
	}
}

static void keyboard_release(struct wl_client *cli, struct wl_resource *kbd)
{
	printf("keyboard_release\n");
}

static struct wl_keyboard_interface keyboard_iface = {
        .release = keyboard_release,
};

/* Touch */
static void touch_kill(struct wl_resource *tch)
{
	sys_cdata_t *cdata = wl_resource_get_user_data(tch);
	list_t *link = list_find(cdata->tchs, tch);
	cdata->tchs = list_remove(cdata->tchs, link, 0);
	if (!cdata->ptrs && !cdata->kbds && !cdata->tchs) {
		list_t *clink = list_find(clients, cdata);
		clients = list_remove(clients, clink, 1);
	}
}

static void touch_release(struct wl_client *cli, struct wl_resource *tch)
{
	printf("touch_release\n");
}

static struct wl_touch_interface touch_iface = {
        .release = touch_release,
};

/* Seat */
static void seat_get_pointer(struct wl_client *cli, struct wl_resource *seat,
		uint32_t id) {
	sys_cdata_t *cdata = find_cdata(cli);
	struct wl_resource *res = wl_resource_create(cli, &wl_pointer_interface, 3, id);
	wl_resource_set_implementation(res, &pointer_iface, cdata, pointer_kill);
	cdata->ptrs = list_insert(cdata->ptrs, res);
	printf("seat_get_pointer - cli=%p cdata=%p ptr=%p\n", cli, cdata, res);
}

static void seat_get_keyboard(struct wl_client *cli, struct wl_resource *seat,
		uint32_t id) {
	sys_cdata_t *cdata = find_cdata(cli);
	struct wl_resource *res = wl_resource_create(cli, &wl_keyboard_interface, 4, id);
	wl_resource_set_implementation(res, &keyboard_iface, cdata, keyboard_kill);

	//map = xkb_keymap_new_from_names(xkb->context, NULL, 0);
	//xkb->context    = xkb_context_new(0);
	//xkb->keymap.map = xkb_keymap_new_from_names(xkb->context, NULL, 0);
	//wl_keyboard_send_keymap(cli, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
	//		keyboard->xkb.keymap.fd,
	//		keyboard->xkb.keymap.size - 1);

	cdata->kbds = list_insert(cdata->kbds, res);
	printf("seat_get_keyboard - cli=%p cdata=%p kbd=%p\n", cli, cdata, res);
}

static void seat_get_touch(struct wl_client *cli, struct wl_resource *seat,
		uint32_t id) {
	sys_cdata_t *cdata = find_cdata(cli);
	struct wl_resource *res = wl_resource_create(cli, &wl_touch_interface, 3, id);
	wl_resource_set_implementation(res, &touch_iface, cdata, touch_kill);
	cdata->tchs = list_insert(cdata->tchs, res);
	printf("seat_get_touch - cli=%p cdata=%p tch=%p\n", cli, cdata, res);
}

static struct wl_seat_interface seat_iface = {
	.get_pointer  = &seat_get_pointer,
	.get_keyboard = &seat_get_keyboard,
        .get_touch    = &seat_get_touch,
};

/*****************************************
 * Wayland Data Device Manager Interface *
 *****************************************/

/* Data Offer */
static void doff_accept(struct wl_client *cli, struct wl_resource *doff,
	       uint32_t serial, const char *mime_type)
{
	printf("doff_accept\n");
}

static void doff_receive(struct wl_client *cli, struct wl_resource *doff,
		const char *mime_type, int32_t fd)
{
	printf("doff_receive\n");
}

static void doff_destroy(struct wl_client *cli, struct wl_resource *doff)
{
	printf("doff_destroy\n");
}

static struct wl_data_offer_interface doff_iface = {
        .accept  = doff_accept,
        .receive = doff_receive,
        .destroy = doff_destroy,
};

/* Data Source */
static void dsrc_offer(struct wl_client *cli, struct wl_resource *dsrc,
	      const char *mime_type)
{
	(void)doff_iface;
	printf("dsrc_offer\n");
}

static void dsrc_destroy(struct wl_client *cli, struct wl_resource *dsrc)
{
	printf("dsrc_destroy\n");
}

static struct wl_data_source_interface dsrc_iface = {
        .offer   = dsrc_offer,
        .destroy = dsrc_destroy,
};

/* Data Device */
static void ddev_start_drag(struct wl_client *cli, struct wl_resource *dsrc,
		   struct wl_resource *source, struct wl_resource *origin,
		   struct wl_resource *icon, uint32_t serial)
{
	printf("start_drag\n");
}

static void ddev_set_selection(struct wl_client *cli, struct wl_resource *dsrc,
		      struct wl_resource *source, uint32_t serial)
{
	printf("set_selection\n");
}

static struct wl_data_device_interface ddev_iface = {
        .start_drag    = ddev_start_drag,
        .set_selection = ddev_set_selection,
};

/* Data Device Manager */
static void ddm_create_data_source(struct wl_client *cli, struct wl_resource *ddm,
		uint32_t id)
{
	printf("ddm_create_data_source\n");
	struct wl_resource *res = wl_resource_create(cli, &wl_data_device_interface, 1, id);
	wl_resource_set_implementation(res, &dsrc_iface, NULL, NULL);
}

static void ddm_get_data_device(struct wl_client *cli, struct wl_resource *ddm,
		uint32_t id, struct wl_resource *seat)
{
	printf("ddm_get_data_device\n");
	struct wl_resource *res = wl_resource_create(cli, &wl_data_device_interface, 1, id);
	wl_resource_set_implementation(res, &ddev_iface, NULL, NULL);
}

static struct wl_data_device_manager_interface ddm_iface = {
	.create_data_source = &ddm_create_data_source,
	.get_data_device    = &ddm_get_data_device,
};

/**************************************
 * Wayland Shell/Compositor Interface *
 **************************************/

/* Surface */
static void surface_kill(struct wl_resource *sfc)
{
	win_t  *win  = wl_resource_get_user_data(sfc);
	list_t *link = list_find(windows, win);
	printf("surface_kill - %p\n", win);
	if (win == NULL)   return;
	if (win == hover)  hover  = NULL;
	if (win == focus)  focus  = NULL;
	if (win == cursor) cursor = NULL;
	free(win->sys);
	free(win);
	windows = list_remove(windows, link, 0);
	wl_resource_set_user_data(sfc, NULL);
	gtk_widget_queue_draw(screen);
}

static void surface_destroy(struct wl_client *cli, struct wl_resource *sfc)
{
	printf("surface_destroy\n");
	surface_kill(sfc);
}

static void surface_attach(struct wl_client *cli, struct wl_resource *sfc,
		struct wl_resource *buf, int32_t x, int32_t y)
{
	win_t *win = wl_resource_get_user_data(sfc);
	printf("surface_attach - %p - %d,%d\n", buf, x, y);
	win->sys->buf = buf;
	win->sys->x   = x;
	win->sys->y   = y;
}

static void surface_damage(struct wl_client *cli, struct wl_resource *sfc,
                   int32_t x, int32_t y, int32_t width, int32_t height)
{
	printf("surface_damage\n");
}

static void surface_frame(struct wl_client *cli, struct wl_resource *sfc,
		uint32_t id)
{
	printf("surface_frame\n");
	struct wl_resource *cb = wl_resource_create(cli, &wl_callback_interface, 1, id);
	wl_resource_set_implementation(cb, NULL, NULL, NULL);
        wl_callback_send_done(cb, get_time());
}

static void surface_set_opaque_region(struct wl_client *cli, struct wl_resource *sfc,
		struct wl_resource *reg)
{
	printf("surface_set_opaque_region\n");
}

static void surface_set_input_region(struct wl_client *cli, struct wl_resource *sfc,
		struct wl_resource *reg)
{
	printf("surface_set_input_region\n");
}

static void surface_commit(struct wl_client *cli, struct wl_resource *sfc)
{
	printf("surface_commit\n");
	gtk_widget_queue_draw(screen);
}

static void surface_set_buffer_transform(struct wl_client *cli, struct wl_resource *sfc,
		int32_t transform)
{
	printf("surface_set_buffer_transform\n");
}

static void surface_set_buffer_scale(struct wl_client *cli, struct wl_resource *sfc,
		int32_t scale)
{
	printf("surface_set_buffer_scale\n");
}

static struct wl_surface_interface surface_iface = {
        .destroy              = surface_destroy,
        .attach               = surface_attach,
        .damage               = surface_damage,
        .frame                = surface_frame,
        .set_opaque_region    = surface_set_opaque_region,
        .set_input_region     = surface_set_input_region,
        .commit               = surface_commit,
        .set_buffer_transform = surface_set_buffer_transform,
        .set_buffer_scale     = surface_set_buffer_scale,
};

/* Region */
static void region_destroy(struct wl_client *cli, struct wl_resource *reg)
{
	printf("region_destroy\n");
}

static void region_add(struct wl_client *cli, struct wl_resource *reg,
                int32_t x, int32_t y, int32_t width, int32_t height)
{
	printf("region_add\n");
}

static void region_subtract(struct wl_client *cli, struct wl_resource *reg,
                     int32_t x, int32_t y, int32_t width, int32_t height)
{
	printf("region_subtract\n");
}

static struct wl_region_interface region_iface = {
        .destroy  = region_destroy,
        .add      = region_add,
        .subtract = region_subtract,
};

/* Compositor */
static void comp_create_surface(struct wl_client *cli, struct wl_resource *comp,
		uint32_t id)
{
	win_t *win = new0(win_t);
	win->sys   = new0(win_sys_t);

	struct wl_resource *res = wl_resource_create(cli, &wl_surface_interface, 3, id);
	wl_resource_set_implementation(res, &surface_iface, win, surface_kill);
	wl_surface_send_enter(res, output);

	win->sys->sfc   = res;
	win->sys->cdata = find_cdata(cli);

	windows = list_insert(windows, win);

	printf("comp_create_surface - cli=%p win=%p cdata=%p\n",
			cli, win, win->sys->cdata);
}

static void comp_create_region(struct wl_client *cli, struct wl_resource *comp,
		uint32_t id)
{
	printf("comp_create_region\n");
	struct wl_resource *res = wl_resource_create(cli, &wl_region_interface, 1, id);
	wl_resource_set_implementation(res, &region_iface, NULL, NULL);
}

static struct wl_compositor_interface comp_iface = {
	.create_surface = comp_create_surface,
	.create_region  = comp_create_region,
};

/* Shell Surface */
static void ssurface_kill(struct wl_resource *ssfc)
{
	win_t *win = wl_resource_get_user_data(ssfc);
	printf("ssurface_kill - %p\n", win);
	win->sys->ssfc = NULL;
	if (!win->sys->xsfc)
		wm_remove(win);
}

static void ssurface_pong(struct wl_client *cli, struct wl_resource *ssfc,
		uint32_t serial)
{
	printf("ssurface_pong\n");
}

static void ssurface_move(struct wl_client *cli, struct wl_resource *ssfc,
		struct wl_resource *seat, uint32_t serial)
{
	printf("ssurface_move\n");
}

static void ssurface_resize(struct wl_client *cli, struct wl_resource *ssfc,
		struct wl_resource *seat, uint32_t serial, uint32_t edges)
{
	printf("ssurface_resize\n");
}

static void ssurface_set_toplevel(struct wl_client *cli, struct wl_resource *ssfc)
{
	printf("ssurface_set_toplevel\n");
}

static void ssurface_set_transient(struct wl_client *cli, struct wl_resource *ssfc,
		struct wl_resource *parent, int32_t x, int32_t y, uint32_t flags)
{
	printf("ssurface_set_transient\n");
}

static void ssurface_set_fullscreen(struct wl_client *cli, struct wl_resource *ssfc,
		uint32_t method, uint32_t framerate, struct wl_resource *out)
{
	printf("ssurface_set_fullscreen\n");
}

static void ssurface_set_popup(struct wl_client *cli, struct wl_resource *ssfc,
		struct wl_resource *seat, uint32_t serial, struct wl_resource *parent,
		int32_t x, int32_t y, uint32_t flags)
{
	printf("ssurface_set_popup\n");
}

static void ssurface_set_maximized(struct wl_client *cli, struct wl_resource *ssfc,
		struct wl_resource *out)
{
	printf("ssurface_set_maximized\n");
}

static void ssurface_set_title(struct wl_client *cli, struct wl_resource *ssfc,
		const char *title)
{
	printf("ssurface_set_title\n");
}

static void ssurface_set_class(struct wl_client *cli, struct wl_resource *ssfc,
		const char *class)
{
	printf("ssurface_set_class\n");
}

static struct wl_shell_surface_interface ssurface_iface = {
        .pong           = ssurface_pong,
        .move           = ssurface_move,
        .resize         = ssurface_resize,
        .set_toplevel   = ssurface_set_toplevel,
        .set_transient  = ssurface_set_transient,
        .set_fullscreen = ssurface_set_fullscreen,
        .set_popup      = ssurface_set_popup,
        .set_maximized  = ssurface_set_maximized,
        .set_title      = ssurface_set_title,
        .set_class      = ssurface_set_class,
};

/* Shell */
static void shell_get_shell_surface(struct wl_client *cli, struct wl_resource *shell, uint32_t id,
                              struct wl_resource *sfc) {
	win_t *win = wl_resource_get_user_data(sfc);
	printf("shell_get_shell_surface - %p\n", win);

	struct wl_resource *res = wl_resource_create(cli, &wl_shell_surface_interface, 1, id);
	wl_resource_set_implementation(res, &ssurface_iface, win, ssurface_kill);

	win->type = TYPE_NORMAL;
	win->sys->ssfc = res;
	wm_insert(win);
}

static struct wl_shell_interface shell_iface = {
	.get_shell_surface = shell_get_shell_surface,
};

/* XDG Popup */
static void xpopup_destroy(struct wl_client *cli, struct wl_resource *xpopup)
{
	printf("xpopup_destroy\n");
}

static struct xdg_popup_interface xpopup_iface = {
	.destroy = xpopup_destroy,
};

/* XDG Surface */
static void xsurface_kill(struct wl_resource *xsfc)
{
	win_t *win = wl_resource_get_user_data(xsfc);
	printf("xsurface_kill - %p\n", win);
	if (!win)
		return;
	win->sys->xsfc = NULL;
	if (!win->sys->ssfc)
		wm_remove(win);
}

static void xsurface_destroy(struct wl_client *cli, struct wl_resource *xsfc)
{
	printf("xsurface_destroy\n");
	xsurface_kill(xsfc);
}

static void xsurface_set_parent(struct wl_client *cli, struct wl_resource *xsfc,
		struct wl_resource *parent)
{
	win_t *win = wl_resource_get_user_data(xsfc);
	printf("xsurface_set_parent - %p\n", win);
	//win->type = TYPE_DIALOG;
}

static void xsurface_set_title(struct wl_client *cli, struct wl_resource *xsfc,
		const char *title)
{
	printf("xsurface_set_title\n");
}

static void xsurface_set_app_id(struct wl_client *cli, struct wl_resource *xsfc,
		const char *app_id)
{
	printf("xsurface_set_app_id\n");
}

static void xsurface_show_window_menu(struct wl_client *cli, struct wl_resource *xsfc,
		struct wl_resource *seat, uint32_t serial, int32_t x, int32_t y)
{
	printf("xsurface_show_window_menu\n");
}

static void xsurface_move(struct wl_client *cli, struct wl_resource *xsfc,
		struct wl_resource *seat, uint32_t serial)
{
	printf("xsurface_move\n");
}

static void xsurface_resize(struct wl_client *cli, struct wl_resource *xsfc,
		struct wl_resource *seat, uint32_t serial, uint32_t edges)
{
	printf("xsurface_resize\n");
}

static void xsurface_ack_configure(struct wl_client *cli, struct wl_resource *xsfc,
		uint32_t serial)
{
	printf("xsurface_ack_configure\n");
}

static void xsurface_set_window_geometry(struct wl_client *cli, struct wl_resource *xsfc,
		int32_t x, int32_t y, int32_t width, int32_t height)
{
	win_t *win = wl_resource_get_user_data(xsfc);
	printf("xsurface_set_window_geometry - %p\n", win);
	win->sys->wx = x;
	win->sys->wy = y;
	win->sys->ww = width;
	win->sys->wh = height;
}

static void xsurface_set_maximized(struct wl_client *cli, struct wl_resource *xsfc)
{
	printf("xsurface_set_maximized\n");
}

static void xsurface_unset_maximized(struct wl_client *cli, struct wl_resource *xsfc)
{
	printf("xsurface_unset_maximized\n");
}

static void xsurface_set_fullscreen(struct wl_client *cli, struct wl_resource *xsfc,
		struct wl_resource *output)
{
	printf("xsurface_set_fullscreen\n");
}

static void xsurface_unset_fullscreen(struct wl_client *cli, struct wl_resource *xsfc)
{
	printf("xsurface_unset_fullscreen\n");
}

static void xsurface_set_minimized(struct wl_client *cli, struct wl_resource *xsfc)
{
	printf("xsurface_set_minimized\n");
}

static struct xdg_surface_interface xsurface_iface = {
	.destroy             = xsurface_destroy,
	.set_parent          = xsurface_set_parent,
	.set_title           = xsurface_set_title,
	.set_app_id          = xsurface_set_app_id,
	.show_window_menu    = xsurface_show_window_menu,
	.move                = xsurface_move,
	.resize              = xsurface_resize,
	.ack_configure       = xsurface_ack_configure,
	.set_window_geometry = xsurface_set_window_geometry,
	.set_maximized       = xsurface_set_maximized,
	.unset_maximized     = xsurface_unset_maximized,
	.set_fullscreen      = xsurface_set_fullscreen,
	.unset_fullscreen    = xsurface_unset_fullscreen,
	.set_minimized       = xsurface_set_minimized,
};

/* XDG Shell */
static void xshell_use_unstable_version(struct wl_client *cli, struct wl_resource *xshell,
		int32_t version)
{
	printf("xshell_use_unstable_version\n");
}

static void xshell_get_xdg_surface(struct wl_client *cli, struct wl_resource *xshell,
		uint32_t id, struct wl_resource *sfc)
{
	win_t *win = wl_resource_get_user_data(sfc);
	printf("xshell_get_xdg_surface - %p\n", win);

	struct wl_resource *res = wl_resource_create(cli, &xdg_surface_interface, 1, id);
	wl_resource_set_implementation(res, &xsurface_iface, win, xsurface_kill);

	win->type = TYPE_NORMAL;
	win->sys->xsfc = res;
	wm_insert(win);
}

static void xshell_get_xdg_popup(struct wl_client *cli, struct wl_resource *xshell,
		uint32_t id, struct wl_resource *sfc, struct wl_resource *parent,
		struct wl_resource *seat, uint32_t serial, int32_t x, int32_t y, uint32_t flags)
{
	win_t *win = wl_resource_get_user_data(sfc);
	printf("xshell_get_xdg_popup - %p @ %d,%d\n", win, x, y);

	struct wl_resource *res = wl_resource_create(cli, &xdg_popup_interface, 1, id);
	wl_resource_set_implementation(res, &xpopup_iface, win, NULL);

	win_t *par  = wl_resource_get_user_data(parent);
	win->type   = TYPE_POPUP;
	win->parent = par;
	win->x      = x;
	win->y      = y;
}

static void xshell_pong(struct wl_client *cli, struct wl_resource *xshell,
		uint32_t serial)
{
	printf("xshell_pong\n");
}

static struct xdg_shell_interface xshell_iface = {
	.use_unstable_version = xshell_use_unstable_version,
	.get_xdg_surface      = xshell_get_xdg_surface,
	.get_xdg_popup        = xshell_get_xdg_popup,
	.pong                 = xshell_pong,
};

/* GTK Surface */
static void gsurface_set_dbus_properties(struct wl_client *cli, struct wl_resource *gsfc,
			    const char *application_id, const char *app_menu_path,
			    const char *menubar_path, const char *window_object_path,
			    const char *application_object_path, const char *unique_bus_name)
{
	printf("gsurface_set_dbus_properties\n");
}

static struct gtk_surface_interface gsurface_iface = {
	.set_dbus_properties = gsurface_set_dbus_properties,
};

/* GTK Shell */
static void gshell_get_gtk_surface(struct wl_client *cli, struct wl_resource *gshell,
		uint32_t id, struct wl_resource *sfc)
{
	printf("gshell_get_gtk_surface\n");
	struct wl_resource *res = wl_resource_create(cli, &gtk_surface_interface, 1, id);
	wl_resource_set_implementation(res, &gsurface_iface, NULL, NULL);
}

static struct gtk_shell_interface gshell_iface = {
	.get_gtk_surface = gshell_get_gtk_surface,
};

/*******************
 * Wayland Globals *
 *******************/

/* References */
static struct wl_global *ref_shm;
static struct wl_global *ref_output;
static struct wl_global *ref_seat;
static struct wl_global *ref_ddm;
static struct wl_global *ref_comp;
static struct wl_global *ref_shell;
static struct wl_global *ref_xshell;
static struct wl_global *ref_gshell;

/* Bind functions */
static void bind_shm(struct wl_client *cli, void *data, uint32_t version, uint32_t id)
{
	printf("bind_shm\n");

	if (version > 1)
		version = 1;

	struct wl_resource *res = wl_resource_create(cli, &wl_shm_interface, version, id);
	wl_resource_set_implementation(res, &shm_iface, NULL, NULL);

	wl_shm_send_format(res, WL_SHM_FORMAT_XRGB8888);
	wl_shm_send_format(res, WL_SHM_FORMAT_ARGB8888);
}

static void bind_output(struct wl_client *cli, void *data, uint32_t version, uint32_t id)
{
	printf("bind_output\n");

	struct wl_resource *res = wl_resource_create(cli, &wl_output_interface, version, id);
	output = res;

	wl_output_send_geometry(res,
			0, 0, 330, 210,              // x/y (px), w/h (mm)
			WL_OUTPUT_SUBPIXEL_UNKNOWN,  // subpixel format
			"unknown", "unknown",        // make, model
			WL_OUTPUT_TRANSFORM_NORMAL); // rotatoin
	wl_output_send_mode(res, WL_OUTPUT_MODE_CURRENT, 800,  600,  60);
	wl_output_send_mode(res, WL_OUTPUT_MODE_CURRENT, 1024, 768,  60);
	wl_output_send_mode(res, WL_OUTPUT_MODE_CURRENT, 1280, 1024, 60);
	wl_output_send_done(res);
}

static void bind_seat(struct wl_client *cli, void *data, uint32_t version, uint32_t id)
{
	printf("bind_seat\n");

	struct wl_resource *res = wl_resource_create(cli, &wl_seat_interface, version, id);
	wl_resource_set_implementation(res, &seat_iface, NULL, NULL);

	wl_seat_send_capabilities(res,
			WL_SEAT_CAPABILITY_KEYBOARD |
			WL_SEAT_CAPABILITY_POINTER);
}

static void bind_ddm(struct wl_client *cli, void *data, uint32_t version, uint32_t id)
{
	printf("bind_ddm\n");

	struct wl_resource *res = wl_resource_create(cli, &wl_data_device_manager_interface, version, id);
	wl_resource_set_implementation(res, &ddm_iface, NULL, NULL);
}

static void bind_comp(struct wl_client *cli, void *data, uint32_t version, uint32_t id)
{
	printf("bind_comp\n");
	struct wl_resource *res = wl_resource_create(cli, &wl_compositor_interface, version, id);
	wl_resource_set_implementation(res, &comp_iface, NULL, NULL);
}

static void bind_shell(struct wl_client *cli, void *data, uint32_t version, uint32_t id)
{
	printf("bind_shell\n");
	struct wl_resource *res = wl_resource_create(cli, &wl_shell_interface, version, id);
	wl_resource_set_implementation(res, &shell_iface, NULL, NULL);
}

static void bind_xshell(struct wl_client *cli, void *data, uint32_t version, uint32_t id)
{
	printf("bind_xshell\n");
	struct wl_resource *res = wl_resource_create(cli, &xdg_shell_interface, version, id);
	wl_resource_set_implementation(res, &xshell_iface, NULL, NULL);
}

static void bind_gshell(struct wl_client *cli, void *data, uint32_t version, uint32_t id)
{
	printf("bind_gshell\n");
	struct wl_resource *res = wl_resource_create(cli, &gtk_shell_interface, version, id);
	wl_resource_set_implementation(res, &gshell_iface, NULL, NULL);
}

/*****************
 * Gtk Callbacks *
 *****************/

static void on_destroy(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	printf("on_destroy\n");
	sys_exit();
}

static gboolean on_key(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	/* Handle special keys */
	if (event->state  &  GDK_CONTROL_MASK &&
	    event->type   == GDK_KEY_PRESS    &&
	    event->keyval == GDK_KEY_Return)
		g_spawn_command_line_async("vte2_90", NULL);

	/* Send key to WM */
	if (focus) {
		event_t ev  = tolower(event->keyval);
		mod_t   mod = get_mod(event->state, event->type == GDK_KEY_RELEASE);
		ptr_t   ptr = get_ptr(focus);
		if (wm_handle_event(focus, ev, mod, ptr))
			return TRUE;
	}

	/* Skip if no focused window */
	if (!focus || !focus->sys->cdata)
		return FALSE;

	/* Send key to wayland */
	printf(g_ascii_isprint(event->keyval)
		? "on_key - win=%p cdata=%p '%c'\n"
		: "on_key - win=%p cdata=%p 0x%X\n",
		focus, focus?focus->sys->cdata:0, event->keyval);
	for (list_t *cur = focus->sys->cdata->kbds; cur; cur = cur->next) {
		uint32_t serial = get_serial();
		uint32_t stamp  = get_time();
		uint32_t key    = event->hardware_keycode-8;
		uint32_t state  = event->type == GDK_KEY_PRESS
			? WL_KEYBOARD_KEY_STATE_PRESSED
			: WL_KEYBOARD_KEY_STATE_RELEASED;
		wl_keyboard_send_key(cur->data, serial, stamp, key, state);
		printf("    send -> %p tm=%d key=%d state=%d\n",
				cur->data, stamp, key, state);
	}
	return TRUE;
}

static gboolean on_button(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	win_t *win = find_win(event->x, event->y);
	printf("on_button - win=%p cdata=%p\n",
			win, win?win->sys->cdata:0);
	if (!win || !win->sys->cdata)
		return FALSE;
	for (list_t *cur = win->sys->cdata->ptrs; cur; cur = cur->next) {
		uint32_t serial = get_serial();
		uint32_t stamp  = get_time();
		uint32_t button = BTN_MOUSE+(event->button-1);
		uint32_t state  = event->type == GDK_BUTTON_PRESS
			? WL_POINTER_BUTTON_STATE_PRESSED
			: WL_POINTER_BUTTON_STATE_RELEASED;
		wl_pointer_send_button(cur->data, serial, stamp, button, state);
		printf("    send -> %p tm=%d btn=%d state=%d\n",
				cur->data, stamp, button, state);
	}
	return TRUE;
}

static gboolean on_move(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
	win_t *win = find_win(event->x, event->y);
	if (!win)
		return FALSE;

	/* Queue draw event for cursor */
	gtk_widget_queue_draw(screen);

	/* Save cursor position */
	cursor_x = event->x;
	cursor_y = event->y;

	/* Send enter/leave */
	if (win != hover) {
		wm_handle_event(hover, EV_LEAVE, MOD(), PTR());
		wm_handle_event(win,   EV_ENTER, MOD(), PTR());
		hover = win;
	}

	/* Sent pointer to WM */
	if (wm_handle_ptr(win, get_ptr(win)))
		return TRUE;

	/* Send motion event to window */
	uint32_t   t = get_time();
	wl_fixed_t x = wl_fixed_from_double(cursor_x - win->x);
	wl_fixed_t y = wl_fixed_from_double(cursor_y - win->y);
	for (list_t *cur = win->sys->cdata->ptrs; cur; cur = cur->next)
		wl_pointer_send_motion(cur->data, t, x, y);
	return TRUE;
}

static void on_size(GtkWidget *widget, GtkAllocation *alloc, gpointer user_data)
{
	printf("on_size - %dx%d\n", alloc->width, alloc->height);
	root->w = alloc->width;
	root->h = alloc->height;
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cairo, gpointer user_data)
{
	printf("on_draw\n");

	//wm_update(); // Hacks for now

	/* Draw windows bottom up */
	list_t *bottom = list_last(windows);
	for (list_t *cur = bottom; cur; cur = cur->prev) {
		win_t       *win   = cur->data;
		if (win->sys->buf == NULL)
			continue;
		if (win->type == TYPE_CURSOR)
			continue;
		sys_bdata_t *bdata = wl_resource_get_user_data(win->sys->buf);
		int x = win->x;
		int y = win->y;
		if (win->type == TYPE_POPUP && win->parent) {
			x += win->parent->x;
			y += win->parent->y;
		}
		//if (win->sys->wx) x -= win->sys->wx;
		//if (win->sys->wy) y -= win->sys->wy;
		printf("    win = %p %dx%d @ %d,%d\n",
				win, win->w, win->h, x, y);
		cairo_surface_mark_dirty(bdata->surface);
		cairo_set_source_surface(cairo, bdata->surface, x, y);
		cairo_paint(cairo);
		//wl_buffer_send_release(win->sys->buf);
		//win->sys->buf = 0;
	}

	/* Draw cursor */
	if (cursor && cursor->sys->buf) {
		int x = cursor_x, y = cursor_y;
		sys_bdata_t *bdata = wl_resource_get_user_data(cursor->sys->buf);
		cairo_surface_mark_dirty(bdata->surface);
		cairo_set_source_surface(cairo, bdata->surface, x, y);
		cairo_paint(cairo);
	}

	return TRUE;
}

static gboolean on_wayland(gpointer user_data)
{
	// TODO - convert to polled execution
	wl_display_flush_clients(display);
	wl_event_loop_dispatch(events, 0);
	return TRUE;
}

/********************
 * System functions *
 ********************/
void sys_move(win_t *win, int x, int y, int w, int h)
{
	if (win->x == x && win->y == y &&
	    win->w == w && win->h == h)
	    	return;
	printf("sys_move: %p - %d,%d  %dx%d\n",
			win, x, y, w, h);

	win->x = x;
	win->y = y;
	win->w = w;
	win->h = h;

	if (win->sys->ssfc)
		wl_shell_surface_send_configure(win->sys->ssfc,
				WL_SHELL_SURFACE_RESIZE_NONE, w, h);

	if (win->sys->xsfc)
		xdg_surface_send_configure(win->sys->xsfc, w, h,
				(win == focus) ? &xstate_active : &xstate_normal,
				get_serial());

	gtk_widget_queue_draw(screen);
}

void sys_raise(win_t *win)
{
	printf("sys_raise: %p\n", win);
}

void sys_focus(win_t *win)
{
	if (win == focus)
		return;
	printf("sys_focus: %p\n", win);

	/* Send leave event */
	uint32_t   s = get_serial();
	wl_fixed_t x = wl_fixed_from_double(cursor_x - win->x);
	wl_fixed_t y = wl_fixed_from_double(cursor_y - win->y);
	if  (focus && focus->sys->sfc && focus->sys->cdata) {
		printf("sys_focus - leave win=%p\n", focus);
		for (list_t *cur = focus->sys->cdata->ptrs; cur; cur = cur->next)
			wl_pointer_send_leave(cur->data, s, focus->sys->sfc);
		for (list_t *cur = focus->sys->cdata->kbds; cur; cur = cur->next)
			wl_keyboard_send_leave(cur->data, s, focus->sys->sfc);
		if (focus->sys->xsfc)
			xdg_surface_send_configure(focus->sys->xsfc, focus->w, focus->h,
					&xstate_normal, get_serial());
	}
	if  (win && win->sys->sfc && win->sys->cdata) {
		printf("sys_focus - enter win=%p\n", win);
		for (list_t *cur = win->sys->cdata->ptrs; cur; cur = cur->next)
			wl_pointer_send_enter(cur->data, s, win->sys->sfc, x, y);
		for (list_t *cur = win->sys->cdata->kbds; cur; cur = cur->next)
			wl_keyboard_send_enter(cur->data, s, win->sys->sfc, &keys);
		if (win->sys->xsfc)
			xdg_surface_send_configure(win->sys->xsfc, win->w, win->h,
					&xstate_active, get_serial());
	}

	/* Update focused window */
	focus = win;
}

void sys_show(win_t *win, state_t state)
{
	printf("sys_show: %p: %d\n", win, state);
}

void sys_watch(win_t *win, event_t ev, mod_t mod)
{
	printf("sys_watch: %p - %x %hhx\n",
			win, ev, mod2int(mod));
}

void sys_unwatch(win_t *win, event_t ev, mod_t mod)
{
	printf("sys_unwatch: %p - %x %hhx\n",
			win, ev, mod2int(mod));
}

list_t *sys_info(win_t *win)
{
	printf("sys_info: %p\n", win);
	return list_insert(NULL, win);
}

win_t *sys_init(void)
{
	printf("sys_init\n");

	/* Create root window */
	root = new0(win_t);
	root->x = 0;
	root->y = 0;
	root->w = 800;
	root->h = 600;

	/* Initalize constants */
	set_array(&xstate_normal, 0);
	set_array(&xstate_active, 1,
		XDG_SURFACE_STATE_ACTIVATED);
	set_array(&xstate_max,2,
		XDG_SURFACE_STATE_ACTIVATED,
		XDG_SURFACE_STATE_MAXIMIZED);
	set_array(&xstate_full, 2,
		XDG_SURFACE_STATE_ACTIVATED,
		XDG_SURFACE_STATE_FULLSCREEN);
	set_array(&xstate_resize, 2,
		XDG_SURFACE_STATE_ACTIVATED,
		XDG_SURFACE_STATE_RESIZING);

	/* Register log handler */
	wl_log_set_handler_server((wl_log_func_t)vprintf);

	/* Open the display */
	if (!(display = wl_display_create()))
		error("Unable to  create display");
	if (wl_display_add_socket(display, NULL) != 0)
		error("Unable to add socket");
	if (!(events = wl_display_get_event_loop(display)))
		error("Unable to get event loop");

	/* Register interfaces */
	ref_shm    = wl_global_create(display, &wl_shm_interface,                 1, NULL, &bind_shm);
	ref_output = wl_global_create(display, &wl_output_interface,              2, NULL, &bind_output);
	ref_ddm    = wl_global_create(display, &wl_data_device_manager_interface, 1, NULL, &bind_ddm);
	ref_shell  = wl_global_create(display, &wl_shell_interface,               1, NULL, &bind_shell);
	ref_comp   = wl_global_create(display, &wl_compositor_interface,          3, NULL, &bind_comp);
	ref_seat   = wl_global_create(display, &wl_seat_interface,                4, NULL, &bind_seat);
	ref_xshell = wl_global_create(display, &xdg_shell_interface,              1, NULL, &bind_xshell);
	ref_gshell = wl_global_create(display, &gtk_shell_interface,              1, NULL, &bind_gshell);

	/* Setup GTK display */
	gtk_init(&conf_argc, &conf_argv);
	screen = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_widget_add_events(screen,
			GDK_KEY_PRESS_MASK |
			GDK_BUTTON_PRESS_MASK |
			GDK_BUTTON_RELEASE_MASK |
			GDK_POINTER_MOTION_MASK);
	g_signal_connect(screen, "destroy",              G_CALLBACK(on_destroy), NULL);
	g_signal_connect(screen, "key-press-event",      G_CALLBACK(on_key),     NULL);
	g_signal_connect(screen, "key-release-event",    G_CALLBACK(on_key),     NULL);
	g_signal_connect(screen, "button-press-event",   G_CALLBACK(on_button),  NULL);
	g_signal_connect(screen, "button-release-event", G_CALLBACK(on_button),  NULL);
	g_signal_connect(screen, "motion-notify-event",  G_CALLBACK(on_move),    NULL);
	g_signal_connect(screen, "size-allocate",        G_CALLBACK(on_size),    NULL);
	g_signal_connect(screen, "draw",                 G_CALLBACK(on_draw),    NULL);
	g_timeout_add(1000/60, on_wayland, NULL);
	gtk_widget_show(screen);

	/* Setup environment */
	setenv("GDK_BACKEND", "wayland", 1);

	return root;
}

void sys_run(win_t *root)
{
	printf("sys_run: %p\n", root);
	gtk_main();
}

void sys_exit(void)
{
	printf("sys_exit\n");
	gtk_main_quit();
}

void sys_free(win_t *root)
{
	printf("sys_free: %p\n", root);
}

