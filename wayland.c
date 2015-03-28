#define WL_DEFINE_TABLES

#include <stdlib.h>
#include <unistd.h>
#include "wayland.h"

int warn(char *msg);

/* Test */
typedef int (*callback_t)(void *obj, uint16_t op, void *data);

typedef struct
{
	uint32_t       max;
	wl_header_t    head;
	wl_request_t   req;
	wl_event_t     evt;
	void          *data;
} msg_t;

typedef struct
{
	uint32_t       id;
	callback_t     func;
	wl_interface_t iface;
} obj_t;

int step(int fd, msg_t *msg)
{
	obj_t *obj = NULL;

	// Read header
	int hlen = sizeof(msg->head);
	if (read(fd, &msg->head, hlen) < hlen)
		warn("short head read");

	// Read header info
	int id  = msg->head.id;
	int len = msg->head.len;
	int op  = msg->head.op;

	// Allocate data
	if (len > msg->max) {
		msg->max  = len;
		msg->data = realloc(msg->data, len);
		if (!msg->data)
			warn("no memory");
	}

	// Read body
	if (read(fd, msg->data, len) < len)
		warn("short data read");

	// Find Object
	(void)id;
	(void)obj;
	int        iface = obj->iface;
	callback_t func  = obj->func;

	// No parsing needed
	if (!wl_rarray[iface] || !wl_rarray[iface][op])
		return func(obj, op, msg->data);

	// Parse body
	const char *info = wl_rarray[iface][op];
	uint32_t *sptr = (uint32_t*) msg->data;
	uint32_t *dptr = (uint32_t*)&msg->req;
	for (int i = 0; info[i]; i++) {
		switch (info[i]) {
			case WL_ARRAY_NONE:
				*dptr++       = *sptr++;  // data
				break;
			case WL_ARRAY_STRING:
				*dptr++       = *sptr++;  // length
				*(void**)dptr =  sptr;    // pointer
				dptr += (((len/4)+1)/4);
				sptr += (sizeof(void*)/4);
				break;
			case WL_ARRAY_ARRAY:
				*dptr++       = *sptr++;  // length
				*(void**)dptr =  sptr;    // pointer
				dptr += ((((len-1)/4)+1)/4);
				sptr += (sizeof(void*)/4);
				break;
			case WL_ARRAY_FD:
				// todo
				break;
		}
	}
	return func(obj, op, &msg->req);
}

int main(int argc, char **argv)
{
	int    fd  = 0;
	msg_t  msg = {};

	while (1) {
		// Find file descriptor
		//if (!select(fd))
		//	warn("error in select");;

		// Process request
		if (!step(fd, &msg))
			warn("error stepping");
	}
}

int main()
{
	wl_init();
	while ((msg = wl_run())) {
		switch msg(
	}
}
