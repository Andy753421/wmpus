#ifndef WAYLAND_H
#define WAYLAND_H

#include <stdint.h>

/***********************************************************
 * Common Types
 ***********************************************************/

#define WL_MESSAGE_LEN 4096

typedef struct {
	uint32_t id;
	uint32_t len : 16;
	uint32_t op  : 16;
} wl_header_t;

typedef struct {
	uint32_t len;
	char    *str;
} wl_string_t;

typedef struct {
	uint32_t len;
	void    *data;
} wl_array_t;

typedef struct {
	uint32_t num  : 24;
	uint32_t frac : 8;
} wl_fixed_t;

/***********************************************************
 * Interfaces
 ***********************************************************/

/* Interface Versions */
#define WL_DISPLAY_VERSION               1
#define WL_REGISTRY_VERSION              1
#define WL_CALLBACK_VERSION              1
#define WL_COMPOSITOR_VERSION            3
#define WL_SHM_POOL_VERSION              1
#define WL_SHM_VERSION                   1
#define WL_BUFFER_VERSION                1
#define WL_DATA_OFFER_VERSION            1
#define WL_DATA_SOURCE_VERSION           1
#define WL_DATA_DEVICE_VERSION           1
#define WL_DATA_DEVICE_MANAGER_VERSION   1
#define WL_SHELL_VERSION                 1
#define WL_SHELL_SURFACE_VERSION         1
#define WL_SURFACE_VERSION               3
#define WL_SEAT_VERSION                  4
#define WL_POINTER_VERSION               3
#define WL_KEYBOARD_VERSION              4
#define WL_TOUCH_VERSION                 3
#define WL_OUTPUT_VERSION                2
#define WL_REGION_VERSION                1
#define WL_SUBCOMPOSITOR_VERSION         1
#define WL_SUBSURFACE_VERSION            1

/* Interface IDs */
typedef enum {
	WL_DISPLAY                     = 0,
	WL_REGISTRY                    = 1,
	WL_CALLBACK                    = 2,
	WL_COMPOSITOR                  = 3,
	WL_SHM_POOL                    = 4,
	WL_SHM                         = 5,
	WL_BUFFER                      = 6,
	WL_DATA_OFFER                  = 7,
	WL_DATA_SOURCE                 = 8,
	WL_DATA_DEVICE                 = 9,
	WL_DATA_DEVICE_MANAGER         = 10,
	WL_SHELL                       = 11,
	WL_SHELL_SURFACE               = 12,
	WL_SURFACE                     = 13,
	WL_SEAT                        = 14,
	WL_POINTER                     = 15,
	WL_KEYBOARD                    = 16,
	WL_TOUCH                       = 17,
	WL_OUTPUT                      = 18,
	WL_REGION                      = 19,
	WL_SUBCOMPOSITOR               = 20,
	WL_SUBSURFACE                  = 21,
	WL_NUM_INTERFACES              = 22,
} wl_interface_t;

/***********************************************************
 * Interface WL_DISPLAY
 ***********************************************************/

/* Request IDs */
typedef enum {
	WL_DISPLAY_SYNC                = 0,
	WL_DISPLAY_GET_REGISTRY        = 1,
	WL_NUM_DISPLAY_REQUESTS        = 2,
} wl_display_request_t;

/* Event IDs */
typedef enum {
	WL_DISPLAY_ERROR               = 0,
	WL_DISPLAY_DELETE_ID           = 1,
	WL_NUM_DISPLAY_EVENTS          = 2,
} wl_display_event_t;

/* Requests Messages */
typedef struct {
	uint32_t    callback;
} wl_display_sync_t;

typedef struct {
	uint32_t    registry;
} wl_display_get_registry_t;

/* Events Messages */
typedef struct {
	uint32_t    object_id;
	uint32_t    code;
	wl_string_t message;
} wl_display_error_t;

typedef struct {
	uint32_t    id;
} wl_display_delete_id_t;

/***********************************************************
 * Interface WL_REGISTRY
 ***********************************************************/

/* Request IDs */
typedef enum {
	WL_REGISTRY_BIND               = 0,
	WL_NUM_REGISTRY_REQUESTS       = 1,
} wl_registry_request_t;

/* Event IDs */
typedef enum {
	WL_REGISTRY_GLOBAL             = 0,
	WL_REGISTRY_GLOBAL_REMOVE      = 1,
	WL_NUM_REGISTRY_EVENTS         = 2,
} wl_registry_event_t;

/* Requests Messages */
typedef struct {
	uint32_t    name;
	uint32_t    id;
} wl_registry_bind_t;

/* Events Messages */
typedef struct {
	uint32_t    name;
	wl_string_t interface;
	uint32_t    version;
} wl_registry_global_t;

typedef struct {
	uint32_t    name;
} wl_registry_global_remove_t;

/***********************************************************
 * Interface WL_CALLBACK
 ***********************************************************/

/* Event IDs */
typedef enum {
	WL_CALLBACK_DONE               = 0,
	WL_NUM_CALLBACK_EVENTS         = 1,
} wl_callback_event_t;

/* Events Messages */
typedef struct {
	uint32_t    callback_data;
} wl_callback_done_t;

/***********************************************************
 * Interface WL_COMPOSITOR
 ***********************************************************/

/* Request IDs */
typedef enum {
	WL_COMPOSITOR_CREATE_SURFACE   = 0,
	WL_COMPOSITOR_CREATE_REGION    = 1,
	WL_NUM_COMPOSITOR_REQUESTS     = 2,
} wl_compositor_request_t;

/* Requests Messages */
typedef struct {
	uint32_t    id;
} wl_compositor_create_surface_t;

typedef struct {
	uint32_t    id;
} wl_compositor_create_region_t;

/***********************************************************
 * Interface WL_SHM_POOL
 ***********************************************************/

/* Request IDs */
typedef enum {
	WL_SHM_POOL_CREATE_BUFFER      = 0,
	WL_SHM_POOL_DESTROY            = 1,
	WL_SHM_POOL_RESIZE             = 2,
	WL_NUM_SHM_POOL_REQUESTS       = 3,
} wl_shm_pool_request_t;

/* Requests Messages */
typedef struct {
	uint32_t    id;
	int32_t     offset;
	int32_t     width;
	int32_t     height;
	int32_t     stride;
	uint32_t    format;
} wl_shm_pool_create_buffer_t;

typedef struct {
	int32_t     size;
} wl_shm_pool_resize_t;

/***********************************************************
 * Interface WL_SHM
 ***********************************************************/

/* Request IDs */
typedef enum {
	WL_SHM_CREATE_POOL             = 0,
	WL_NUM_SHM_REQUESTS            = 1,
} wl_shm_request_t;

/* Event IDs */
typedef enum {
	WL_SHM_FORMAT                  = 0,
	WL_NUM_SHM_EVENTS              = 1,
} wl_shm_event_t;

/* Requests Messages */
typedef struct {
	uint32_t    id;
	int32_t     size;
} wl_shm_create_pool_t;

/* Events Messages */
typedef struct {
	uint32_t    format;
} wl_shm_format_t;

/***********************************************************
 * Interface WL_BUFFER
 ***********************************************************/

/* Request IDs */
typedef enum {
	WL_BUFFER_DESTROY              = 0,
	WL_NUM_BUFFER_REQUESTS         = 1,
} wl_buffer_request_t;

/* Event IDs */
typedef enum {
	WL_BUFFER_RELEASE              = 0,
	WL_NUM_BUFFER_EVENTS           = 1,
} wl_buffer_event_t;

/***********************************************************
 * Interface WL_DATA_OFFER
 ***********************************************************/

/* Request IDs */
typedef enum {
	WL_DATA_OFFER_ACCEPT           = 0,
	WL_DATA_OFFER_RECEIVE          = 1,
	WL_DATA_OFFER_DESTROY          = 2,
	WL_NUM_DATA_OFFER_REQUESTS     = 3,
} wl_data_offer_request_t;

/* Event IDs */
typedef enum {
	WL_DATA_OFFER_OFFER            = 0,
	WL_NUM_DATA_OFFER_EVENTS       = 1,
} wl_data_offer_event_t;

/* Requests Messages */
typedef struct {
	uint32_t    serial;
	wl_string_t mime_type;
} wl_data_offer_accept_t;

typedef struct {
	wl_string_t mime_type;
} wl_data_offer_receive_t;

/* Events Messages */
typedef struct {
	wl_string_t mime_type;
} wl_data_offer_offer_t;

/***********************************************************
 * Interface WL_DATA_SOURCE
 ***********************************************************/

/* Request IDs */
typedef enum {
	WL_DATA_SOURCE_OFFER           = 0,
	WL_DATA_SOURCE_DESTROY         = 1,
	WL_NUM_DATA_SOURCE_REQUESTS    = 2,
} wl_data_source_request_t;

/* Event IDs */
typedef enum {
	WL_DATA_SOURCE_TARGET          = 0,
	WL_DATA_SOURCE_SEND            = 1,
	WL_DATA_SOURCE_CANCELLED       = 2,
	WL_NUM_DATA_SOURCE_EVENTS      = 3,
} wl_data_source_event_t;

/* Requests Messages */
typedef struct {
	wl_string_t mime_type;
} wl_data_source_offer_t;

/* Events Messages */
typedef struct {
	wl_string_t mime_type;
} wl_data_source_target_t;

typedef struct {
	wl_string_t mime_type;
} wl_data_source_send_t;

/***********************************************************
 * Interface WL_DATA_DEVICE
 ***********************************************************/

/* Request IDs */
typedef enum {
	WL_DATA_DEVICE_START_DRAG      = 0,
	WL_DATA_DEVICE_SET_SELECTION   = 1,
	WL_NUM_DATA_DEVICE_REQUESTS    = 2,
} wl_data_device_request_t;

/* Event IDs */
typedef enum {
	WL_DATA_DEVICE_DATA_OFFER      = 0,
	WL_DATA_DEVICE_ENTER           = 1,
	WL_DATA_DEVICE_LEAVE           = 2,
	WL_DATA_DEVICE_MOTION          = 3,
	WL_DATA_DEVICE_DROP            = 4,
	WL_DATA_DEVICE_SELECTION       = 5,
	WL_NUM_DATA_DEVICE_EVENTS      = 6,
} wl_data_device_event_t;

/* Requests Messages */
typedef struct {
	uint32_t    source;
	uint32_t    origin;
	uint32_t    icon;
	uint32_t    serial;
} wl_data_device_start_drag_t;

typedef struct {
	uint32_t    source;
	uint32_t    serial;
} wl_data_device_set_selection_t;

/* Events Messages */
typedef struct {
	uint32_t    id;
} wl_data_device_data_offer_t;

typedef struct {
	uint32_t    serial;
	uint32_t    surface;
	wl_fixed_t  x;
	wl_fixed_t  y;
	uint32_t    id;
} wl_data_device_enter_t;

typedef struct {
	uint32_t    time;
	wl_fixed_t  x;
	wl_fixed_t  y;
} wl_data_device_motion_t;

typedef struct {
	uint32_t    id;
} wl_data_device_selection_t;

/***********************************************************
 * Interface WL_DATA_DEVICE_MANAGER
 ***********************************************************/

/* Request IDs */
typedef enum {
	WL_DATA_DEVICE_MANAGER_CREATE_DATA_SOURCE = 0,
	WL_DATA_DEVICE_MANAGER_GET_DATA_DEVICE = 1,
	WL_NUM_DATA_DEVICE_MANAGER_REQUESTS = 2,
} wl_data_device_manager_request_t;

/* Requests Messages */
typedef struct {
	uint32_t    id;
} wl_data_device_manager_create_data_source_t;

typedef struct {
	uint32_t    id;
	uint32_t    seat;
} wl_data_device_manager_get_data_device_t;

/***********************************************************
 * Interface WL_SHELL
 ***********************************************************/

/* Request IDs */
typedef enum {
	WL_SHELL_GET_SHELL_SURFACE     = 0,
	WL_NUM_SHELL_REQUESTS          = 1,
} wl_shell_request_t;

/* Requests Messages */
typedef struct {
	uint32_t    id;
	uint32_t    surface;
} wl_shell_get_shell_surface_t;

/***********************************************************
 * Interface WL_SHELL_SURFACE
 ***********************************************************/

/* Request IDs */
typedef enum {
	WL_SHELL_SURFACE_PONG          = 0,
	WL_SHELL_SURFACE_MOVE          = 1,
	WL_SHELL_SURFACE_RESIZE        = 2,
	WL_SHELL_SURFACE_SET_TOPLEVEL  = 3,
	WL_SHELL_SURFACE_SET_TRANSIENT = 4,
	WL_SHELL_SURFACE_SET_FULLSCREEN = 5,
	WL_SHELL_SURFACE_SET_POPUP     = 6,
	WL_SHELL_SURFACE_SET_MAXIMIZED = 7,
	WL_SHELL_SURFACE_SET_TITLE     = 8,
	WL_SHELL_SURFACE_SET_CLASS     = 9,
	WL_NUM_SHELL_SURFACE_REQUESTS  = 10,
} wl_shell_surface_request_t;

/* Event IDs */
typedef enum {
	WL_SHELL_SURFACE_PING          = 0,
	WL_SHELL_SURFACE_CONFIGURE     = 1,
	WL_SHELL_SURFACE_POPUP_DONE    = 2,
	WL_NUM_SHELL_SURFACE_EVENTS    = 3,
} wl_shell_surface_event_t;

/* Requests Messages */
typedef struct {
	uint32_t    serial;
} wl_shell_surface_pong_t;

typedef struct {
	uint32_t    seat;
	uint32_t    serial;
} wl_shell_surface_move_t;

typedef struct {
	uint32_t    seat;
	uint32_t    serial;
	uint32_t    edges;
} wl_shell_surface_resize_t;

typedef struct {
	uint32_t    parent;
	int32_t     x;
	int32_t     y;
	uint32_t    flags;
} wl_shell_surface_set_transient_t;

typedef struct {
	uint32_t    method;
	uint32_t    framerate;
	uint32_t    output;
} wl_shell_surface_set_fullscreen_t;

typedef struct {
	uint32_t    seat;
	uint32_t    serial;
	uint32_t    parent;
	int32_t     x;
	int32_t     y;
	uint32_t    flags;
} wl_shell_surface_set_popup_t;

typedef struct {
	uint32_t    output;
} wl_shell_surface_set_maximized_t;

typedef struct {
	wl_string_t title;
} wl_shell_surface_set_title_t;

typedef struct {
	wl_string_t class_;
} wl_shell_surface_set_class_t;

/* Events Messages */
typedef struct {
	uint32_t    serial;
} wl_shell_surface_ping_t;

typedef struct {
	uint32_t    edges;
	int32_t     width;
	int32_t     height;
} wl_shell_surface_configure_t;

/***********************************************************
 * Interface WL_SURFACE
 ***********************************************************/

/* Request IDs */
typedef enum {
	WL_SURFACE_DESTROY             = 0,
	WL_SURFACE_ATTACH              = 1,
	WL_SURFACE_DAMAGE              = 2,
	WL_SURFACE_FRAME               = 3,
	WL_SURFACE_SET_OPAQUE_REGION   = 4,
	WL_SURFACE_SET_INPUT_REGION    = 5,
	WL_SURFACE_COMMIT              = 6,
	WL_SURFACE_SET_BUFFER_TRANSFORM = 7,
	WL_SURFACE_SET_BUFFER_SCALE    = 8,
	WL_NUM_SURFACE_REQUESTS        = 9,
} wl_surface_request_t;

/* Event IDs */
typedef enum {
	WL_SURFACE_ENTER               = 0,
	WL_SURFACE_LEAVE               = 1,
	WL_NUM_SURFACE_EVENTS          = 2,
} wl_surface_event_t;

/* Requests Messages */
typedef struct {
	uint32_t    buffer;
	int32_t     x;
	int32_t     y;
} wl_surface_attach_t;

typedef struct {
	int32_t     x;
	int32_t     y;
	int32_t     width;
	int32_t     height;
} wl_surface_damage_t;

typedef struct {
	uint32_t    callback;
} wl_surface_frame_t;

typedef struct {
	uint32_t    region;
} wl_surface_set_opaque_region_t;

typedef struct {
	uint32_t    region;
} wl_surface_set_input_region_t;

typedef struct {
	int32_t     transform;
} wl_surface_set_buffer_transform_t;

typedef struct {
	int32_t     scale;
} wl_surface_set_buffer_scale_t;

/* Events Messages */
typedef struct {
	uint32_t    output;
} wl_surface_enter_t;

typedef struct {
	uint32_t    output;
} wl_surface_leave_t;

/***********************************************************
 * Interface WL_SEAT
 ***********************************************************/

/* Request IDs */
typedef enum {
	WL_SEAT_GET_POINTER            = 0,
	WL_SEAT_GET_KEYBOARD           = 1,
	WL_SEAT_GET_TOUCH              = 2,
	WL_NUM_SEAT_REQUESTS           = 3,
} wl_seat_request_t;

/* Event IDs */
typedef enum {
	WL_SEAT_CAPABILITIES           = 0,
	WL_SEAT_NAME                   = 1,
	WL_NUM_SEAT_EVENTS             = 2,
} wl_seat_event_t;

/* Requests Messages */
typedef struct {
	uint32_t    id;
} wl_seat_get_pointer_t;

typedef struct {
	uint32_t    id;
} wl_seat_get_keyboard_t;

typedef struct {
	uint32_t    id;
} wl_seat_get_touch_t;

/* Events Messages */
typedef struct {
	uint32_t    capabilities;
} wl_seat_capabilities_t;

typedef struct {
	wl_string_t name;
} wl_seat_name_t;

/***********************************************************
 * Interface WL_POINTER
 ***********************************************************/

/* Request IDs */
typedef enum {
	WL_POINTER_SET_CURSOR          = 0,
	WL_POINTER_RELEASE             = 1,
	WL_NUM_POINTER_REQUESTS        = 2,
} wl_pointer_request_t;

/* Event IDs */
typedef enum {
	WL_POINTER_ENTER               = 0,
	WL_POINTER_LEAVE               = 1,
	WL_POINTER_MOTION              = 2,
	WL_POINTER_BUTTON              = 3,
	WL_POINTER_AXIS                = 4,
	WL_NUM_POINTER_EVENTS          = 5,
} wl_pointer_event_t;

/* Requests Messages */
typedef struct {
	uint32_t    serial;
	uint32_t    surface;
	int32_t     hotspot_x;
	int32_t     hotspot_y;
} wl_pointer_set_cursor_t;

/* Events Messages */
typedef struct {
	uint32_t    serial;
	uint32_t    surface;
	wl_fixed_t  surface_x;
	wl_fixed_t  surface_y;
} wl_pointer_enter_t;

typedef struct {
	uint32_t    serial;
	uint32_t    surface;
} wl_pointer_leave_t;

typedef struct {
	uint32_t    time;
	wl_fixed_t  surface_x;
	wl_fixed_t  surface_y;
} wl_pointer_motion_t;

typedef struct {
	uint32_t    serial;
	uint32_t    time;
	uint32_t    button;
	uint32_t    state;
} wl_pointer_button_t;

typedef struct {
	uint32_t    time;
	uint32_t    axis;
	wl_fixed_t  value;
} wl_pointer_axis_t;

/***********************************************************
 * Interface WL_KEYBOARD
 ***********************************************************/

/* Request IDs */
typedef enum {
	WL_KEYBOARD_RELEASE            = 0,
	WL_NUM_KEYBOARD_REQUESTS       = 1,
} wl_keyboard_request_t;

/* Event IDs */
typedef enum {
	WL_KEYBOARD_KEYMAP             = 0,
	WL_KEYBOARD_ENTER              = 1,
	WL_KEYBOARD_LEAVE              = 2,
	WL_KEYBOARD_KEY                = 3,
	WL_KEYBOARD_MODIFIERS          = 4,
	WL_KEYBOARD_REPEAT_INFO        = 5,
	WL_NUM_KEYBOARD_EVENTS         = 6,
} wl_keyboard_event_t;

/* Events Messages */
typedef struct {
	uint32_t    format;
	uint32_t    size;
} wl_keyboard_keymap_t;

typedef struct {
	uint32_t    serial;
	uint32_t    surface;
	wl_array_t  keys;
} wl_keyboard_enter_t;

typedef struct {
	uint32_t    serial;
	uint32_t    surface;
} wl_keyboard_leave_t;

typedef struct {
	uint32_t    serial;
	uint32_t    time;
	uint32_t    key;
	uint32_t    state;
} wl_keyboard_key_t;

typedef struct {
	uint32_t    serial;
	uint32_t    mods_depressed;
	uint32_t    mods_latched;
	uint32_t    mods_locked;
	uint32_t    group;
} wl_keyboard_modifiers_t;

typedef struct {
	int32_t     rate;
	int32_t     delay;
} wl_keyboard_repeat_info_t;

/***********************************************************
 * Interface WL_TOUCH
 ***********************************************************/

/* Request IDs */
typedef enum {
	WL_TOUCH_RELEASE               = 0,
	WL_NUM_TOUCH_REQUESTS          = 1,
} wl_touch_request_t;

/* Event IDs */
typedef enum {
	WL_TOUCH_DOWN                  = 0,
	WL_TOUCH_UP                    = 1,
	WL_TOUCH_MOTION                = 2,
	WL_TOUCH_FRAME                 = 3,
	WL_TOUCH_CANCEL                = 4,
	WL_NUM_TOUCH_EVENTS            = 5,
} wl_touch_event_t;

/* Events Messages */
typedef struct {
	uint32_t    serial;
	uint32_t    time;
	uint32_t    surface;
	int32_t     id;
	wl_fixed_t  x;
	wl_fixed_t  y;
} wl_touch_down_t;

typedef struct {
	uint32_t    serial;
	uint32_t    time;
	int32_t     id;
} wl_touch_up_t;

typedef struct {
	uint32_t    time;
	int32_t     id;
	wl_fixed_t  x;
	wl_fixed_t  y;
} wl_touch_motion_t;

/***********************************************************
 * Interface WL_OUTPUT
 ***********************************************************/

/* Event IDs */
typedef enum {
	WL_OUTPUT_GEOMETRY             = 0,
	WL_OUTPUT_MODE                 = 1,
	WL_OUTPUT_DONE                 = 2,
	WL_OUTPUT_SCALE                = 3,
	WL_NUM_OUTPUT_EVENTS           = 4,
} wl_output_event_t;

/* Events Messages */
typedef struct {
	int32_t     x;
	int32_t     y;
	int32_t     physical_width;
	int32_t     physical_height;
	int32_t     subpixel;
	wl_string_t make;
	wl_string_t model;
	int32_t     transform;
} wl_output_geometry_t;

typedef struct {
	uint32_t    flags;
	int32_t     width;
	int32_t     height;
	int32_t     refresh;
} wl_output_mode_t;

typedef struct {
	int32_t     factor;
} wl_output_scale_t;

/***********************************************************
 * Interface WL_REGION
 ***********************************************************/

/* Request IDs */
typedef enum {
	WL_REGION_DESTROY              = 0,
	WL_REGION_ADD                  = 1,
	WL_REGION_SUBTRACT             = 2,
	WL_NUM_REGION_REQUESTS         = 3,
} wl_region_request_t;

/* Requests Messages */
typedef struct {
	int32_t     x;
	int32_t     y;
	int32_t     width;
	int32_t     height;
} wl_region_add_t;

typedef struct {
	int32_t     x;
	int32_t     y;
	int32_t     width;
	int32_t     height;
} wl_region_subtract_t;

/***********************************************************
 * Interface WL_SUBCOMPOSITOR
 ***********************************************************/

/* Request IDs */
typedef enum {
	WL_SUBCOMPOSITOR_DESTROY       = 0,
	WL_SUBCOMPOSITOR_GET_SUBSURFACE = 1,
	WL_NUM_SUBCOMPOSITOR_REQUESTS  = 2,
} wl_subcompositor_request_t;

/* Requests Messages */
typedef struct {
	uint32_t    id;
	uint32_t    surface;
	uint32_t    parent;
} wl_subcompositor_get_subsurface_t;

/***********************************************************
 * Interface WL_SUBSURFACE
 ***********************************************************/

/* Request IDs */
typedef enum {
	WL_SUBSURFACE_DESTROY          = 0,
	WL_SUBSURFACE_SET_POSITION     = 1,
	WL_SUBSURFACE_PLACE_ABOVE      = 2,
	WL_SUBSURFACE_PLACE_BELOW      = 3,
	WL_SUBSURFACE_SET_SYNC         = 4,
	WL_SUBSURFACE_SET_DESYNC       = 5,
	WL_NUM_SUBSURFACE_REQUESTS     = 6,
} wl_subsurface_request_t;

/* Requests Messages */
typedef struct {
	int32_t     x;
	int32_t     y;
} wl_subsurface_set_position_t;

typedef struct {
	uint32_t    sibling;
} wl_subsurface_place_above_t;

typedef struct {
	uint32_t    sibling;
} wl_subsurface_place_below_t;

/* Union messages */
typedef union {
	wl_display_sync_t                                  wl_display_sync;
	wl_display_get_registry_t                          wl_display_get_registry;
	wl_registry_bind_t                                 wl_registry_bind;
	wl_compositor_create_surface_t                     wl_compositor_create_surface;
	wl_compositor_create_region_t                      wl_compositor_create_region;
	wl_shm_pool_create_buffer_t                        wl_shm_pool_create_buffer;
	wl_shm_pool_resize_t                               wl_shm_pool_resize;
	wl_shm_create_pool_t                               wl_shm_create_pool;
	wl_data_offer_accept_t                             wl_data_offer_accept;
	wl_data_offer_receive_t                            wl_data_offer_receive;
	wl_data_source_offer_t                             wl_data_source_offer;
	wl_data_device_start_drag_t                        wl_data_device_start_drag;
	wl_data_device_set_selection_t                     wl_data_device_set_selection;
	wl_data_device_manager_create_data_source_t        wl_data_device_manager_create_data_source;
	wl_data_device_manager_get_data_device_t           wl_data_device_manager_get_data_device;
	wl_shell_get_shell_surface_t                       wl_shell_get_shell_surface;
	wl_shell_surface_pong_t                            wl_shell_surface_pong;
	wl_shell_surface_move_t                            wl_shell_surface_move;
	wl_shell_surface_resize_t                          wl_shell_surface_resize;
	wl_shell_surface_set_transient_t                   wl_shell_surface_set_transient;
	wl_shell_surface_set_fullscreen_t                  wl_shell_surface_set_fullscreen;
	wl_shell_surface_set_popup_t                       wl_shell_surface_set_popup;
	wl_shell_surface_set_maximized_t                   wl_shell_surface_set_maximized;
	wl_shell_surface_set_title_t                       wl_shell_surface_set_title;
	wl_shell_surface_set_class_t                       wl_shell_surface_set_class;
	wl_surface_attach_t                                wl_surface_attach;
	wl_surface_damage_t                                wl_surface_damage;
	wl_surface_frame_t                                 wl_surface_frame;
	wl_surface_set_opaque_region_t                     wl_surface_set_opaque_region;
	wl_surface_set_input_region_t                      wl_surface_set_input_region;
	wl_surface_set_buffer_transform_t                  wl_surface_set_buffer_transform;
	wl_surface_set_buffer_scale_t                      wl_surface_set_buffer_scale;
	wl_seat_get_pointer_t                              wl_seat_get_pointer;
	wl_seat_get_keyboard_t                             wl_seat_get_keyboard;
	wl_seat_get_touch_t                                wl_seat_get_touch;
	wl_pointer_set_cursor_t                            wl_pointer_set_cursor;
	wl_region_add_t                                    wl_region_add;
	wl_region_subtract_t                               wl_region_subtract;
	wl_subcompositor_get_subsurface_t                  wl_subcompositor_get_subsurface;
	wl_subsurface_set_position_t                       wl_subsurface_set_position;
	wl_subsurface_place_above_t                        wl_subsurface_place_above;
	wl_subsurface_place_below_t                        wl_subsurface_place_below;
} wl_request_t;

typedef union {
	wl_display_error_t                                 wl_display_error;
	wl_display_delete_id_t                             wl_display_delete_id;
	wl_registry_global_t                               wl_registry_global;
	wl_registry_global_remove_t                        wl_registry_global_remove;
	wl_callback_done_t                                 wl_callback_done;
	wl_shm_format_t                                    wl_shm_format;
	wl_data_offer_offer_t                              wl_data_offer_offer;
	wl_data_source_target_t                            wl_data_source_target;
	wl_data_source_send_t                              wl_data_source_send;
	wl_data_device_data_offer_t                        wl_data_device_data_offer;
	wl_data_device_enter_t                             wl_data_device_enter;
	wl_data_device_motion_t                            wl_data_device_motion;
	wl_data_device_selection_t                         wl_data_device_selection;
	wl_shell_surface_ping_t                            wl_shell_surface_ping;
	wl_shell_surface_configure_t                       wl_shell_surface_configure;
	wl_surface_enter_t                                 wl_surface_enter;
	wl_surface_leave_t                                 wl_surface_leave;
	wl_seat_capabilities_t                             wl_seat_capabilities;
	wl_seat_name_t                                     wl_seat_name;
	wl_pointer_enter_t                                 wl_pointer_enter;
	wl_pointer_leave_t                                 wl_pointer_leave;
	wl_pointer_motion_t                                wl_pointer_motion;
	wl_pointer_button_t                                wl_pointer_button;
	wl_pointer_axis_t                                  wl_pointer_axis;
	wl_keyboard_keymap_t                               wl_keyboard_keymap;
	wl_keyboard_enter_t                                wl_keyboard_enter;
	wl_keyboard_leave_t                                wl_keyboard_leave;
	wl_keyboard_key_t                                  wl_keyboard_key;
	wl_keyboard_modifiers_t                            wl_keyboard_modifiers;
	wl_keyboard_repeat_info_t                          wl_keyboard_repeat_info;
	wl_touch_down_t                                    wl_touch_down;
	wl_touch_up_t                                      wl_touch_up;
	wl_touch_motion_t                                  wl_touch_motion;
	wl_output_geometry_t                               wl_output_geometry;
	wl_output_mode_t                                   wl_output_mode;
	wl_output_scale_t                                  wl_output_scale;
} wl_event_t;

/***********************************************************
 * Arrays and Strings
 ***********************************************************/

/* Constants */
#define WL_ARRAY_NONE   '-'
#define WL_ARRAY_STRING 's'
#define WL_ARRAY_ARRAY  'a'
#define WL_ARRAY_FD     'f'

extern const char **wl_rarray[WL_NUM_INTERFACES];
extern const char **wl_earray[WL_NUM_INTERFACES];

/* Request Array */
#ifdef WL_DEFINE_TABLES
const char **wl_rarray[WL_NUM_INTERFACES] = {
	[WL_SHM] (const char *[WL_NUM_SHM_REQUESTS]) {
		[WL_SHM_CREATE_POOL]           "-f-",
	},
	[WL_DATA_OFFER] (const char *[WL_NUM_DATA_OFFER_REQUESTS]) {
		[WL_DATA_OFFER_ACCEPT]         "-s",
		[WL_DATA_OFFER_RECEIVE]        "sf",
	},
	[WL_DATA_SOURCE] (const char *[WL_NUM_DATA_SOURCE_REQUESTS]) {
		[WL_DATA_SOURCE_OFFER]         "s",
	},
	[WL_SHELL_SURFACE] (const char *[WL_NUM_SHELL_SURFACE_REQUESTS]) {
		[WL_SHELL_SURFACE_SET_TITLE]   "s",
		[WL_SHELL_SURFACE_SET_CLASS]   "s",
	},
};
#endif

/* Event Array */
#ifdef WL_DEFINE_TABLES
const char **wl_earray[WL_NUM_INTERFACES] = {
	[WL_DISPLAY] (const char *[WL_NUM_DISPLAY_EVENTS]) {
		[WL_DISPLAY_ERROR]             "--s",
	},
	[WL_REGISTRY] (const char *[WL_NUM_REGISTRY_EVENTS]) {
		[WL_REGISTRY_GLOBAL]           "-s-",
	},
	[WL_DATA_OFFER] (const char *[WL_NUM_DATA_OFFER_EVENTS]) {
		[WL_DATA_OFFER_OFFER]          "s",
	},
	[WL_DATA_SOURCE] (const char *[WL_NUM_DATA_SOURCE_EVENTS]) {
		[WL_DATA_SOURCE_TARGET]        "s",
		[WL_DATA_SOURCE_SEND]          "sf",
	},
	[WL_SEAT] (const char *[WL_NUM_SEAT_EVENTS]) {
		[WL_SEAT_NAME]                 "s",
	},
	[WL_KEYBOARD] (const char *[WL_NUM_KEYBOARD_EVENTS]) {
		[WL_KEYBOARD_KEYMAP]           "-f-",
		[WL_KEYBOARD_ENTER]            "--a",
	},
	[WL_OUTPUT] (const char *[WL_NUM_OUTPUT_EVENTS]) {
		[WL_OUTPUT_GEOMETRY]           "-----ss-",
	},
};
#endif

#endif
