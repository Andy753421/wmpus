/*
 * Copyright (c) 2014, Andy Spencer <andy753421@gmail.com>
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
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>

#include <xkbcommon/xkbcommon.h>
#include <wayland-server.h>
#include <wayland-client.h>

#include <gtk/gtk.h>

#include "util.h"
#include "conf.h"
#include "sys.h"
#include "wm.h"

/* Internal structures */
struct win_sys {
	win_t *win;
};

typedef struct {
	uint8_t *mem;
	size_t   size;
} sys_pool_t;

typedef struct {
	sys_pool_t      *pool;
	uint8_t         *mem;
	cairo_surface_t *surface;
} sys_buf_t;

struct wl_resource *output;

sys_pool_t *gdata[10];
int         gidx;

/* Global data */
static struct wl_display    *display;
static struct wl_event_loop *events;

static GtkWidget            *window;
static sys_buf_t            *buffer;

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
	printf(g_ascii_isprint(event->keyval)
		? "on_key: '%c'\n"
		: "on_key: 0x%X\n",
		event->keyval);
	if (event->keyval == GDK_KEY_q)
		sys_exit();
	if (event->keyval == GDK_KEY_t)
		g_spawn_command_line_async("st-wl", NULL);
	return TRUE;
}

static gboolean on_button(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	printf("on_button\n");
	return TRUE;
}

static gboolean on_move(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
	//printf("on_motion\n");
	return TRUE;
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cairo, gpointer user_data)
{
	printf("on_draw\n");

	if (buffer) {
		cairo_surface_mark_dirty(buffer->surface);
		cairo_set_source_surface(cairo, buffer->surface, 10, 10);
		cairo_paint(cairo);
	}

	cairo_set_source_rgb(cairo, 1, 1, 1);
	cairo_arc(cairo, 150, 150, 50, 0, 2*G_PI);
	cairo_fill(cairo);

	//int gi = 0, mi;
	//FILE *fd = fopen("/tmp/mem.txt", "w+");
	//for (gi = 0; gi < 10; gi++) {
	//	mi = 0;
	//	while (gdata[gi] && (mi+32) < gdata[gi]->size) {
	//		fprintf(fd, "%d %p %08x: ", gi, gdata[gi]->mem, mi);
	//		for (int i = 0; i < 16; mi++, i += 2)
	//			fprintf(fd, "%02hhx%02hhx ",
	//				gdata[gi]->mem[mi+0],
	//				gdata[gi]->mem[mi+1]);
	//		fprintf(fd, "\n");
	//	}
	//	fprintf(fd, "\n");
	//}
	//printf("\n");
	//fclose(fd);

	return TRUE;
}

static gboolean on_wayland(gpointer user_data)
{
	// TODO - convert to polled execution
	wl_display_flush_clients(display);
	wl_event_loop_dispatch(events, 0);
	return TRUE;
}

/*********************
 * Wayland Callbacks *
 *********************/

#if 0
static void new_window(void)
{
	printf("new_window\n");
}

static void new_screen(void)
{
	printf("new_screen\n");
}
#endif

/****************************
 * Wayland Buffer Interface *
 ****************************/
static struct wl_buffer_interface buffer_iface = {
        .destroy  = NULL,
};

/***********************************
 * Wayland Shared Memory Interface *
 ***********************************/

/* Shm Pool */
static void pool_create_buffer(struct wl_client *cli, struct wl_resource *pool,
		uint32_t id, int32_t offset, int32_t width, int32_t height,
		int32_t stride, uint32_t format)
{
	sys_buf_t  *buf  = new0(sys_buf_t);
	buf->pool = wl_resource_get_user_data(pool);
	buf->mem  = buf->pool->mem + offset;

	printf("pool_create_buffer - %dx%d %p+%d : %d,%d,%d\n",
			width, height, buf->pool->mem, offset, id, stride, format);

	if (offset > buf->pool->size || offset < 0)
	{
		printf("\n\nerror\n\n");
		wl_resource_post_error(pool, WL_SHM_ERROR_INVALID_STRIDE,
				"offset is too big or negative");
		return;
	}

	cairo_format_t cf =
		format == WL_SHM_FORMAT_ARGB8888 ? CAIRO_FORMAT_ARGB32 :
		format == WL_SHM_FORMAT_XRGB8888 ? CAIRO_FORMAT_RGB24  : CAIRO_FORMAT_INVALID;

	buf->surface = cairo_image_surface_create_for_data(buf->mem, cf, width, height, stride);

	struct wl_resource *res = wl_resource_create(cli, &wl_buffer_interface,
                                        wl_resource_get_version(pool), id);
	wl_resource_set_implementation(res, &buffer_iface, buf, NULL);
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
	gdata[gidx++] = data;
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
	gdata[gidx++] = data;
}

static struct wl_shm_interface shm_iface = {
	.create_pool = shm_create_pool,
};

/**************************
 * Wayland Seat Interface *
 **************************/

/* Pointer */
static struct wl_pointer_interface pointer_iface = {
        .set_cursor = NULL,
        .release    = NULL,
};

/* Keyboard */
static struct wl_keyboard_interface keyboard_iface = {
        .release    = NULL,
};

/* Touch */
static struct wl_touch_interface touch_iface = {
        .release    = NULL,
};

/* Seat */
static void seat_get_pointer(struct wl_client *cli, struct wl_resource *seat,
		uint32_t id) {
	printf("seat_get_pointer\n");
	struct wl_resource *res = wl_resource_create(cli, &wl_pointer_interface, 3, id);
	wl_resource_set_implementation(res, &pointer_iface, NULL, NULL);
}

static void seat_get_keyboard(struct wl_client *cli, struct wl_resource *seat,
		uint32_t id) {
	printf("seat_get_keyboard\n");
	struct wl_resource *res = wl_resource_create(cli, &wl_keyboard_interface, 3, id);
	wl_resource_set_implementation(res, &keyboard_iface, NULL, NULL);
}

static void seat_get_touch(struct wl_client *cli, struct wl_resource *seat,
		uint32_t id) {
	printf("seat_get_touch\n");
	struct wl_resource *res = wl_resource_create(cli, &wl_touch_interface, 3, id);
	wl_resource_set_implementation(res, &touch_iface, NULL, NULL);
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
static struct wl_data_offer_interface doff_iface = {
        .accept        = NULL,
        .receive       = NULL,
        .destroy       = NULL,
};

/* Data Source */
static struct wl_data_source_interface dsrc_iface = {
        .offer         = NULL,
        .destroy       = NULL,
};

/* Data Device */
static struct wl_data_device_interface ddev_iface = {
        .start_drag    = NULL,
        .set_selection = NULL,
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

/* Callback */
static void frame_callback(struct wl_resource *res)
{
	printf("frame_callback\n");
}

/* Surface */
static void surface_destroy(struct wl_client *cli, struct wl_resource *res)
{
	printf("surface_destroy\n");
}

static void surface_attach(struct wl_client *cli, struct wl_resource *res,
		struct wl_resource *buf, int32_t x, int32_t y)
{
	sys_buf_t *data = wl_resource_get_user_data(buf);
	printf("surface_attach - %p\n", data->pool->mem);
	buffer = data;
}

static void surface_damage(struct wl_client *cli, struct wl_resource *res,
                   int32_t x, int32_t y, int32_t width, int32_t height)
{
	printf("surface_damage\n");
}

static void surface_frame(struct wl_client *cli, struct wl_resource *res,
		uint32_t id)
{
	printf("surface_frame\n");
	struct wl_resource *cb = wl_resource_create(cli, &wl_callback_interface, 1, id);
	wl_resource_set_implementation(cb, NULL, NULL, &frame_callback);
        wl_callback_send_done(cb, time(NULL));
}

static void surface_set_opaque_region(struct wl_client *cli, struct wl_resource *res,
		struct wl_resource *reg)
{
	printf("surface_set_opaque_region\n");
}

static void surface_set_input_region(struct wl_client *cli, struct wl_resource *res,
		struct wl_resource *reg)
{
	printf("surface_set_input_region\n");
}

static void surface_commit(struct wl_client *cli, struct wl_resource *res)
{
	printf("surface_commit\n");
	gtk_widget_queue_draw(window);
}

static void surface_set_buffer_transform(struct wl_client *cli, struct wl_resource *res,
		int32_t transform)
{
	printf("surface_set_buffer_transform\n");
}

static void surface_set_buffer_scale(struct wl_client *cli, struct wl_resource *res,
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
static void region_destroy(struct wl_client *cli, struct wl_resource *res)
{
	printf("region_destroy\n");
}

static void region_add(struct wl_client *cli, struct wl_resource *res,
                int32_t x, int32_t y, int32_t width, int32_t height)
{
	printf("region_add\n");
}

static void region_subtract(struct wl_client *cli, struct wl_resource *res,
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
	printf("comp_create_surface\n");
	struct wl_resource *res = wl_resource_create(cli, &wl_surface_interface, 3, id);
	wl_resource_set_implementation(res, &surface_iface, NULL, NULL);
	wl_surface_send_enter(res, output);
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
static void ssurface_pong(struct wl_client *cli, struct wl_resource *res,
		uint32_t serial)
{
	printf("ssurface_pong\n");
}

static void ssurface_move(struct wl_client *cli, struct wl_resource *res,
		struct wl_resource *seat, uint32_t serial)
{
	printf("ssurface_move\n");
}

static void ssurface_resize(struct wl_client *cli, struct wl_resource *res,
		struct wl_resource *seat, uint32_t serial, uint32_t edges)
{
	printf("ssurface_resize\n");
}

static void ssurface_set_toplevel(struct wl_client *cli, struct wl_resource *res)
{
	printf("ssurface_set_toplevel\n");
}

static void ssurface_set_transient(struct wl_client *cli, struct wl_resource *res,
		struct wl_resource *parent, int32_t x, int32_t y, uint32_t flags)
{
	printf("ssurface_set_transient\n");
}

static void ssurface_set_fullscreen(struct wl_client *cli, struct wl_resource *res,
		uint32_t method, uint32_t framerate, struct wl_resource *out)
{
	printf("ssurface_set_fullscreen\n");
}

static void ssurface_set_popup(struct wl_client *cli, struct wl_resource *res,
		struct wl_resource *seat, uint32_t serial, struct wl_resource *parent,
		int32_t x, int32_t y, uint32_t flags)
{
	printf("ssurface_set_popup\n");
}

static void ssurface_set_maximized(struct wl_client *cli, struct wl_resource *res,
		struct wl_resource *out)
{
	printf("ssurface_set_maximized\n");
}

static void ssurface_set_title(struct wl_client *cli, struct wl_resource *res,
		const char *title)
{
	printf("ssurface_set_title\n");
}

static void ssurface_set_class(struct wl_client *cli, struct wl_resource *res,
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
	printf("shell_get_shell_surface\n");
	struct wl_resource *res = wl_resource_create(cli, &wl_shell_surface_interface, 1, id);
	wl_resource_set_implementation(res, &ssurface_iface, NULL, NULL);
}

static struct wl_shell_interface shell_iface = {
	.get_shell_surface = shell_get_shell_surface,
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
	//wl_output_send_mode(res, WL_OUTPUT_MODE_CURRENT, 800,  600,  60);
	//wl_output_send_mode(res, WL_OUTPUT_MODE_CURRENT, 1024, 768,  60);
	wl_output_send_mode(res, WL_OUTPUT_MODE_CURRENT, 1280, 1024, 60);
	//wl_output_send_done(res);
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

/********************
 * System functions *
 ********************/
void sys_move(win_t *win, int x, int y, int w, int h)
{
	printf("sys_move: %p - %d,%d  %dx%d\n",
			win, x, y, w, h);
}

void sys_raise(win_t *win)
{
	printf("sys_raise: %p\n", win);
}

void sys_focus(win_t *win)
{
	printf("sys_focus: %p\n", win);
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
	ref_seat   = wl_global_create(display, &wl_seat_interface,                3, NULL, &bind_seat);

	/* Setup GTK display */
	gtk_init(&conf_argc, &conf_argv);
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_widget_add_events(window,
			GDK_KEY_PRESS_MASK |
			GDK_BUTTON_PRESS_MASK |
			GDK_BUTTON_RELEASE_MASK |
			GDK_POINTER_MOTION_MASK);
	g_signal_connect(window, "destroy",             G_CALLBACK(on_destroy), NULL);
	g_signal_connect(window, "key-press-event",     G_CALLBACK(on_key),     NULL);
	g_signal_connect(window, "button-press-event",  G_CALLBACK(on_button),  NULL);
	g_signal_connect(window, "motion-notify-event", G_CALLBACK(on_move),    NULL);
	g_signal_connect(window, "draw",                G_CALLBACK(on_draw),    NULL);
	g_timeout_add(1000/60, on_wayland, NULL);
	gtk_widget_show(window);

	return new0(win_t);
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

