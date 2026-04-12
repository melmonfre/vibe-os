#ifndef POSIX_GFX_COMPAT_H
#define POSIX_GFX_COMPAT_H

#include <stdint.h>

#define VIBE_XCOMPAT_ABI_VERSION 1u
#define VIBE_XCOMPAT_PROTOCOL_ENDIAN_LITTLE 1u
#define VIBE_XCOMPAT_REQUEST_FLAG_REPLY_EXPECTED (1u << 0)
#define VIBE_XCOMPAT_REQUEST_FLAG_RESERVED_MASK  0xFFFFFFFEu

enum vibe_xcompat_opcode {
    VIBE_XCOMPAT_OP_NONE = 0,
    VIBE_XCOMPAT_OP_CONNECT = 1,
    VIBE_XCOMPAT_OP_DISCONNECT = 2,
    VIBE_XCOMPAT_OP_CREATE_WINDOW = 3,
    VIBE_XCOMPAT_OP_DESTROY_WINDOW = 4,
    VIBE_XCOMPAT_OP_MAP_WINDOW = 5,
    VIBE_XCOMPAT_OP_UNMAP_WINDOW = 6,
    VIBE_XCOMPAT_OP_CONFIGURE_WINDOW = 7,
    VIBE_XCOMPAT_OP_SELECT_INPUT = 8,
    VIBE_XCOMPAT_OP_CREATE_PIXMAP = 9,
    VIBE_XCOMPAT_OP_FREE_PIXMAP = 10,
    VIBE_XCOMPAT_OP_CREATE_GC = 11,
    VIBE_XCOMPAT_OP_FREE_GC = 12,
    VIBE_XCOMPAT_OP_DRAW_PRIMITIVE = 13,
    VIBE_XCOMPAT_OP_PUT_IMAGE = 14,
    VIBE_XCOMPAT_OP_COPY_AREA = 15,
    VIBE_XCOMPAT_OP_SET_WINDOW_TITLE = 16,
    VIBE_XCOMPAT_OP_INTERN_ATOM = 17,
    VIBE_XCOMPAT_OP_SET_WM_PROTOCOLS = 18,
    VIBE_XCOMPAT_OP_FLUSH = 19,
    VIBE_XCOMPAT_OP_SYNC = 20,
    VIBE_XCOMPAT_OP_NEXT_EVENT = 21,
    VIBE_XCOMPAT_OP_PENDING = 22
};

enum vibe_xcompat_resource_kind {
    VIBE_XCOMPAT_RESOURCE_NONE = 0,
    VIBE_XCOMPAT_RESOURCE_WINDOW = 1,
    VIBE_XCOMPAT_RESOURCE_PIXMAP = 2,
    VIBE_XCOMPAT_RESOURCE_GC = 3,
    VIBE_XCOMPAT_RESOURCE_CURSOR = 4,
    VIBE_XCOMPAT_RESOURCE_ATOM = 5
};

enum vibe_xcompat_draw_kind {
    VIBE_XCOMPAT_DRAW_NONE = 0,
    VIBE_XCOMPAT_DRAW_POINT = 1,
    VIBE_XCOMPAT_DRAW_LINE = 2,
    VIBE_XCOMPAT_DRAW_RECTANGLE = 3,
    VIBE_XCOMPAT_DRAW_FILL_RECTANGLE = 4,
    VIBE_XCOMPAT_DRAW_TEXT8 = 5
};

enum vibe_xcompat_event_type {
    VIBE_XCOMPAT_EVENT_NONE = 0,
    VIBE_XCOMPAT_EVENT_EXPOSE = 1,
    VIBE_XCOMPAT_EVENT_KEY_PRESS = 2,
    VIBE_XCOMPAT_EVENT_KEY_RELEASE = 3,
    VIBE_XCOMPAT_EVENT_BUTTON_PRESS = 4,
    VIBE_XCOMPAT_EVENT_BUTTON_RELEASE = 5,
    VIBE_XCOMPAT_EVENT_MOTION = 6,
    VIBE_XCOMPAT_EVENT_ENTER = 7,
    VIBE_XCOMPAT_EVENT_LEAVE = 8,
    VIBE_XCOMPAT_EVENT_FOCUS_IN = 9,
    VIBE_XCOMPAT_EVENT_FOCUS_OUT = 10,
    VIBE_XCOMPAT_EVENT_CLIENT_MESSAGE = 11,
    VIBE_XCOMPAT_EVENT_DESTROY = 12,
    VIBE_XCOMPAT_EVENT_CONFIGURE = 13
};

struct vibe_xcompat_header {
    uint16_t abi_version;
    uint16_t opcode;
    uint32_t size_bytes;
    uint32_t request_id;
    uint32_t flags;
    uint32_t reserved0;
};

struct vibe_xcompat_connect_request {
    struct vibe_xcompat_header header;
    uint32_t client_caps;
    uint32_t reserved[7];
};

struct vibe_xcompat_connect_reply {
    int32_t status;
    uint32_t server_caps;
    uint32_t default_screen;
    uint32_t root_window_id;
    uint32_t event_queue_depth;
    uint32_t protocol_endian;
    uint32_t reserved[6];
};

struct vibe_xcompat_window_request {
    struct vibe_xcompat_header header;
    uint32_t window_id;
    uint32_t parent_id;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t border_width;
    uint32_t event_mask;
    uint32_t background_pixel;
    uint32_t border_pixel;
    uint32_t reserved[4];
};

struct vibe_xcompat_configure_window_request {
    struct vibe_xcompat_header header;
    uint32_t window_id;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t value_mask;
    uint32_t reserved[5];
};

struct vibe_xcompat_gc_request {
    struct vibe_xcompat_header header;
    uint32_t gc_id;
    uint32_t drawable_id;
    uint32_t value_mask;
    uint32_t foreground;
    uint32_t background;
    uint32_t line_width;
    uint32_t reserved[4];
};

struct vibe_xcompat_draw_request {
    struct vibe_xcompat_header header;
    uint32_t draw_kind;
    uint32_t drawable_id;
    uint32_t gc_id;
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
    uint32_t width;
    uint32_t height;
    uint32_t reserved[4];
};

struct vibe_xcompat_image_request {
    struct vibe_xcompat_header header;
    uint32_t drawable_id;
    uint32_t gc_id;
    uint32_t width;
    uint32_t height;
    int32_t dst_x;
    int32_t dst_y;
    uint32_t depth;
    uint32_t format;
    uint32_t byte_count;
    uint32_t transfer_id;
    uint32_t reserved[3];
};

struct vibe_xcompat_copy_area_request {
    struct vibe_xcompat_header header;
    uint32_t src_drawable_id;
    uint32_t dst_drawable_id;
    uint32_t gc_id;
    int32_t src_x;
    int32_t src_y;
    uint32_t width;
    uint32_t height;
    int32_t dst_x;
    int32_t dst_y;
    uint32_t reserved[4];
};

struct vibe_xcompat_atom_request {
    struct vibe_xcompat_header header;
    uint32_t atom_id;
    uint32_t name_length;
    uint32_t only_if_exists;
    uint32_t reserved[5];
};

struct vibe_xcompat_window_title_request {
    struct vibe_xcompat_header header;
    uint32_t window_id;
    uint32_t title_length;
    uint32_t transfer_id;
    uint32_t reserved[5];
};

struct vibe_xcompat_status_reply {
    int32_t status;
    uint32_t reserved[7];
};

struct vibe_xcompat_pending_reply {
    int32_t status;
    uint32_t pending_events;
    uint32_t reserved[6];
};

struct vibe_xcompat_event {
    uint32_t abi_version;
    uint32_t event_type;
    uint32_t sequence;
    uint32_t window_id;
    uint32_t detail;
    uint32_t state;
    uint32_t time_ms;
    int32_t x;
    int32_t y;
    int32_t x_root;
    int32_t y_root;
    uint32_t data0;
    uint32_t data1;
    uint32_t data2;
    uint32_t reserved[4];
};

#endif
