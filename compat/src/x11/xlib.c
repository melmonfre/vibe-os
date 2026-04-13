#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#include <compat/posix/errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <include/userland_api.h>

#define X11_COMPAT_MAX_WINDOWS 16
#define X11_COMPAT_MAX_PIXMAPS 16
#define X11_COMPAT_EVENT_QUEUE 32
#define X11_COMPAT_TITLE_MAX 127
#define X11_COMPAT_ROOT_WINDOW 1ul
#define X11_COMPAT_FIRST_WINDOW 2ul
#define X11_COMPAT_FIRST_PIXMAP 0x100ul

struct _XGC {
    unsigned long foreground;
    unsigned long background;
    int line_width;
};

struct x11_compat_window {
    Window id;
    int active;
    int mapped;
    int x;
    int y;
    unsigned int width;
    unsigned int height;
    unsigned long background_pixel;
    unsigned long border_pixel;
    long event_mask;
    char title[X11_COMPAT_TITLE_MAX + 1];
};

struct x11_compat_pixmap {
    Pixmap id;
    int active;
    unsigned int width;
    unsigned int height;
    unsigned int depth;
};

struct _XDisplay {
    int connection_fd;
    int default_screen;
    Window root_window;
    struct video_mode mode;
    unsigned long serial;
    struct x11_compat_window windows[X11_COMPAT_MAX_WINDOWS];
    struct x11_compat_pixmap pixmaps[X11_COMPAT_MAX_PIXMAPS];
    XEvent event_queue[X11_COMPAT_EVENT_QUEUE];
    int event_count;
    uint8_t last_buttons;
    int last_mouse_x;
    int last_mouse_y;
};

static int x11_syscall5(int num, int a, int b, int c, int d, int e) {
    int ret;

    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                     : "memory", "cc");
    return ret;
}

static int x11_fetch_mode(struct video_mode *mode) {
    if (mode == 0) {
        return -1;
    }
    return x11_syscall5(SYSCALL_GFX_INFO, (int)(uintptr_t)mode, 0, 0, 0, 0);
}

static int x11_next_input_event(struct input_event *event) {
    if (event == 0) {
        return -1;
    }
    return x11_syscall5(SYSCALL_INPUT_EVENT, (int)(uintptr_t)event, 0, 0, 0, 0);
}

static unsigned int x11_ticks(void) {
    return (unsigned int)x11_syscall5(SYSCALL_TIME_TICKS, 0, 0, 0, 0, 0);
}

static unsigned int x11_clock_hz(void) {
    return 100u;
}

static unsigned long x11_millis(void) {
    return (unsigned long)(((unsigned long long)x11_ticks() * 1000ull) /
                           (unsigned long long)x11_clock_hz());
}

static void x11_yield(void) {
    (void)x11_syscall5(SYSCALL_YIELD, 0, 0, 0, 0, 0);
}

static void x11_rect(int x, int y, int w, int h, uint8_t color) {
    (void)x11_syscall5(SYSCALL_GFX_RECT, x, y, w, h, (int)color);
}

static void x11_text(int x, int y, uint8_t color, const char *text) {
    (void)x11_syscall5(SYSCALL_GFX_TEXT,
                       x,
                       y,
                       (int)(uintptr_t)text,
                       (int)color,
                       0);
}

static void x11_present(void) {
    (void)x11_syscall5(SYSCALL_GFX_FLIP, 0, 0, 0, 0, 0);
}

static struct x11_compat_window *x11_find_window(Display *display, Window id) {
    int i;

    if (display == 0) {
        return 0;
    }
    if (id == X11_COMPAT_ROOT_WINDOW) {
        return 0;
    }
    for (i = 0; i < X11_COMPAT_MAX_WINDOWS; ++i) {
        if (display->windows[i].active && display->windows[i].id == id) {
            return &display->windows[i];
        }
    }
    return 0;
}

static struct x11_compat_pixmap *x11_find_pixmap(Display *display, Pixmap id) {
    int i;

    if (display == 0) {
        return 0;
    }
    for (i = 0; i < X11_COMPAT_MAX_PIXMAPS; ++i) {
        if (display->pixmaps[i].active && display->pixmaps[i].id == id) {
            return &display->pixmaps[i];
        }
    }
    return 0;
}

static unsigned long x11_gc_foreground(GC gc) {
    if (gc == 0) {
        return 15ul;
    }
    return gc->foreground & 0xfful;
}

static int x11_queue_event(Display *display, const XEvent *event) {
    if (display == 0 || event == 0) {
        return -1;
    }
    if (display->event_count >= X11_COMPAT_EVENT_QUEUE) {
        memmove(&display->event_queue[0],
                &display->event_queue[1],
                (size_t)(X11_COMPAT_EVENT_QUEUE - 1) * sizeof(XEvent));
        display->event_count = X11_COMPAT_EVENT_QUEUE - 1;
    }
    display->event_queue[display->event_count++] = *event;
    return 0;
}

static void x11_make_any_event(Display *display, XEvent *event, int type, Window window) {
    memset(event, 0, sizeof(*event));
    event->xany.type = type;
    event->xany.display = display;
    event->xany.window = window;
    event->xany.serial = ++display->serial;
    event->xany.send_event = False;
}

static void x11_queue_expose(Display *display, struct x11_compat_window *window) {
    XEvent event;

    if (display == 0 || window == 0 || (window->event_mask & ExposureMask) == 0) {
        return;
    }
    x11_make_any_event(display, &event, Expose, window->id);
    event.xexpose.x = 0;
    event.xexpose.y = 0;
    event.xexpose.width = (int)window->width;
    event.xexpose.height = (int)window->height;
    event.xexpose.count = 0;
    (void)x11_queue_event(display, &event);
}

static void x11_queue_focus(Display *display, struct x11_compat_window *window, int type) {
    XEvent event;

    if (display == 0 || window == 0 || (window->event_mask & FocusChangeMask) == 0) {
        return;
    }
    x11_make_any_event(display, &event, type, window->id);
    (void)x11_queue_event(display, &event);
}

static void x11_fill_window_background(Display *display, struct x11_compat_window *window) {
    if (display == 0 || window == 0 || !window->mapped) {
        return;
    }
    x11_rect(window->x,
             window->y,
             (int)window->width,
             (int)window->height,
             (uint8_t)(window->background_pixel & 0xfful));
    if (window->title[0] != '\0') {
        x11_text(window->x + 4, window->y + 4, 15u, window->title);
    }
    x11_present();
}

static int x11_window_accepts_pointer(const struct x11_compat_window *window) {
    if (window == 0 || !window->mapped) {
        return 0;
    }
    return (window->event_mask & (ButtonPressMask | ButtonReleaseMask |
                                  PointerMotionMask | EnterWindowMask |
                                  LeaveWindowMask)) != 0;
}

static struct x11_compat_window *x11_pick_pointer_window(Display *display, int x, int y) {
    int i;

    if (display == 0) {
        return 0;
    }
    for (i = X11_COMPAT_MAX_WINDOWS - 1; i >= 0; --i) {
        struct x11_compat_window *window = &display->windows[i];

        if (!x11_window_accepts_pointer(window)) {
            continue;
        }
        if (x >= window->x && y >= window->y &&
            x < window->x + (int)window->width &&
            y < window->y + (int)window->height) {
            return window;
        }
    }
    return 0;
}

static void x11_pump_input(Display *display) {
    struct input_event input;
    struct x11_compat_window *window;
    XEvent event;
    uint8_t changed;

    if (display == 0 || display->event_count >= X11_COMPAT_EVENT_QUEUE) {
        return;
    }
    if (x11_next_input_event(&input) == 0) {
        return;
    }

    if (input.type == INPUT_EVENT_KEY) {
        int type = input.value >= 0 ? KeyPress : KeyRelease;
        unsigned int keycode = (unsigned int)(input.value >= 0 ? input.value : -input.value);
        int i;

        for (i = X11_COMPAT_MAX_WINDOWS - 1; i >= 0; --i) {
            window = &display->windows[i];
            if (!window->active || !window->mapped) {
                continue;
            }
            if ((type == KeyPress && (window->event_mask & KeyPressMask) == 0) ||
                (type == KeyRelease && (window->event_mask & KeyReleaseMask) == 0)) {
                continue;
            }
            x11_make_any_event(display, &event, type, window->id);
            event.xkey.root = display->root_window;
            event.xkey.subwindow = None;
            event.xkey.time = (Time)x11_millis();
            event.xkey.x = 0;
            event.xkey.y = 0;
            event.xkey.x_root = display->last_mouse_x;
            event.xkey.y_root = display->last_mouse_y;
            event.xkey.state = display->last_buttons;
            event.xkey.keycode = keycode;
            event.xkey.same_screen = True;
            (void)x11_queue_event(display, &event);
            break;
        }
        return;
    }

    if (input.type != INPUT_EVENT_MOUSE) {
        return;
    }

    display->last_mouse_x = input.mouse.x;
    display->last_mouse_y = input.mouse.y;
    window = x11_pick_pointer_window(display, input.mouse.x, input.mouse.y);
    if (window != 0 && (window->event_mask & PointerMotionMask) != 0 &&
        (input.mouse.dx != 0 || input.mouse.dy != 0)) {
        x11_make_any_event(display, &event, MotionNotify, window->id);
        event.xmotion.root = display->root_window;
        event.xmotion.subwindow = None;
        event.xmotion.time = (Time)x11_millis();
        event.xmotion.x = input.mouse.x - window->x;
        event.xmotion.y = input.mouse.y - window->y;
        event.xmotion.x_root = input.mouse.x;
        event.xmotion.y_root = input.mouse.y;
        event.xmotion.state = input.mouse.buttons;
        event.xmotion.button = 0u;
        event.xmotion.same_screen = True;
        (void)x11_queue_event(display, &event);
    }

    changed = (uint8_t)(display->last_buttons ^ input.mouse.buttons);
    if (window != 0) {
        int button;
        for (button = 0; button < 3; ++button) {
            uint8_t mask = (uint8_t)(1u << button);
            int type;

            if ((changed & mask) == 0) {
                continue;
            }
            type = (input.mouse.buttons & mask) != 0 ? ButtonPress : ButtonRelease;
            if ((type == ButtonPress && (window->event_mask & ButtonPressMask) == 0) ||
                (type == ButtonRelease && (window->event_mask & ButtonReleaseMask) == 0)) {
                continue;
            }
            x11_make_any_event(display, &event, type, window->id);
            event.xbutton.root = display->root_window;
            event.xbutton.subwindow = None;
            event.xbutton.time = (Time)x11_millis();
            event.xbutton.x = input.mouse.x - window->x;
            event.xbutton.y = input.mouse.y - window->y;
            event.xbutton.x_root = input.mouse.x;
            event.xbutton.y_root = input.mouse.y;
            event.xbutton.state = input.mouse.buttons;
            event.xbutton.button = (unsigned int)(button + 1);
            event.xbutton.same_screen = True;
            (void)x11_queue_event(display, &event);
        }
    }
    display->last_buttons = input.mouse.buttons;
}

static int x11_wait_for_event(Display *display) {
    unsigned int start;

    if (display == 0) {
        errno = EINVAL;
        return -1;
    }
    start = x11_ticks();
    while (display->event_count == 0) {
        x11_pump_input(display);
        if (display->event_count != 0) {
            break;
        }
        if ((unsigned int)(x11_ticks() - start) > x11_clock_hz() / 20u) {
            x11_yield();
            start = x11_ticks();
        }
    }
    return 0;
}

static int x11_pop_event(Display *display, XEvent *event_return, int remove_event) {
    if (display == 0 || event_return == 0) {
        errno = EINVAL;
        return 0;
    }
    if (display->event_count == 0) {
        x11_pump_input(display);
    }
    if (display->event_count == 0) {
        return 0;
    }
    *event_return = display->event_queue[0];
    if (remove_event) {
        memmove(&display->event_queue[0],
                &display->event_queue[1],
                (size_t)(display->event_count - 1) * sizeof(XEvent));
        --display->event_count;
    }
    return 1;
}

static uint8_t x11_color8(unsigned long pixel) {
    return (uint8_t)(pixel & 0xfful);
}

static void x11_draw_line_basic(int x0, int y0, int x1, int y1, uint8_t color) {
    int dx = x1 - x0;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int err = (dx > 0 ? dx : -dx) + (dy > 0 ? -dy : dy);

    for (;;) {
        x11_rect(x0, y0, 1, 1, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        if ((err << 1) >= dy) {
            err += dy;
            x0 += sx;
        }
        if ((err << 1) <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void x11_draw_arc_basic(int fill,
                               int x,
                               int y,
                               unsigned int width,
                               unsigned int height,
                               uint8_t color) {
    int rx;
    int ry;
    int px;
    int py;

    if (width == 0u || height == 0u) {
        return;
    }
    rx = (int)width / 2;
    ry = (int)height / 2;
    if (rx <= 0 || ry <= 0) {
        return;
    }
    for (py = -ry; py <= ry; ++py) {
        for (px = -rx; px <= rx; ++px) {
            long lhs = (long)px * (long)px * (long)ry * (long)ry +
                       (long)py * (long)py * (long)rx * (long)rx;
            long rhs = (long)rx * (long)rx * (long)ry * (long)ry;

            if (fill) {
                if (lhs <= rhs) {
                    x11_rect(x + rx + px, y + ry + py, 1, 1, color);
                }
            } else {
                long delta = lhs > rhs ? lhs - rhs : rhs - lhs;
                if (delta <= (long)rx * (long)ry) {
                    x11_rect(x + rx + px, y + ry + py, 1, 1, color);
                }
            }
        }
    }
}

Display *XOpenDisplay(const char *display_name) {
    Display *display;

    (void)display_name;
    display = (Display *)calloc(1u, sizeof(Display));
    if (display == 0) {
        errno = ENOMEM;
        return 0;
    }
    display->connection_fd = 1;
    display->default_screen = 0;
    display->root_window = X11_COMPAT_ROOT_WINDOW;
    display->last_buttons = 0u;
    if (x11_fetch_mode(&display->mode) != 0) {
        display->mode.width = 640u;
        display->mode.height = 480u;
        display->mode.pitch = 640u;
        display->mode.bpp = 8u;
    }
    return display;
}

int XCloseDisplay(Display *display) {
    if (display == 0) {
        errno = EINVAL;
        return 0;
    }
    free(display);
    return 0;
}

int XConnectionNumber(Display *display) {
    if (display == 0) {
        errno = EINVAL;
        return -1;
    }
    return display->connection_fd;
}

int DefaultScreen(Display *display) {
    if (display == 0) {
        errno = EINVAL;
        return 0;
    }
    return display->default_screen;
}

Window RootWindow(Display *display, int screen_number) {
    (void)screen_number;
    if (display == 0) {
        errno = EINVAL;
        return None;
    }
    return display->root_window;
}

Colormap DefaultColormap(Display *display, int screen_number) {
    (void)display;
    (void)screen_number;
    return 0;
}

unsigned long BlackPixel(Display *display, int screen_number) {
    (void)display;
    (void)screen_number;
    return 0ul;
}

unsigned long WhitePixel(Display *display, int screen_number) {
    (void)display;
    (void)screen_number;
    return 15ul;
}

int DisplayWidth(Display *display, int screen_number) {
    (void)screen_number;
    if (display == 0) {
        errno = EINVAL;
        return 0;
    }
    return (int)display->mode.width;
}

int DisplayHeight(Display *display, int screen_number) {
    (void)screen_number;
    if (display == 0) {
        errno = EINVAL;
        return 0;
    }
    return (int)display->mode.height;
}

int XPending(Display *display) {
    if (display == 0) {
        errno = EINVAL;
        return 0;
    }
    x11_pump_input(display);
    return display->event_count;
}

int XNextEvent(Display *display, XEvent *event_return) {
    if (x11_wait_for_event(display) != 0) {
        return 0;
    }
    return x11_pop_event(display, event_return, 1);
}

int XPeekEvent(Display *display, XEvent *event_return) {
    if (x11_wait_for_event(display) != 0) {
        return 0;
    }
    return x11_pop_event(display, event_return, 0);
}

int XSelectInput(Display *display, Window window, long event_mask) {
    struct x11_compat_window *target = x11_find_window(display, window);

    if (target == 0) {
        errno = EINVAL;
        return 0;
    }
    target->event_mask = event_mask;
    return 1;
}

Window XCreateSimpleWindow(Display *display, Window parent, int x, int y,
                           unsigned int width, unsigned int height,
                           unsigned int border_width, unsigned long border,
                           unsigned long background) {
    XSetWindowAttributes attributes;

    memset(&attributes, 0, sizeof(attributes));
    attributes.background_pixel = background;
    attributes.border_pixel = border;
    return XCreateWindow(display,
                         parent,
                         x,
                         y,
                         width,
                         height,
                         border_width,
                         0,
                         InputOutput,
                         0,
                         CWBackPixel | CWBorderPixel,
                         &attributes);
}

Window XCreateWindow(Display *display, Window parent, int x, int y,
                     unsigned int width, unsigned int height,
                     unsigned int border_width, int depth,
                     unsigned int class, Visual *visual,
                     unsigned long valuemask,
                     XSetWindowAttributes *attributes) {
    int i;

    (void)parent;
    (void)border_width;
    (void)depth;
    (void)class;
    (void)visual;
    if (display == 0) {
        errno = EINVAL;
        return None;
    }
    for (i = 0; i < X11_COMPAT_MAX_WINDOWS; ++i) {
        if (!display->windows[i].active) {
            struct x11_compat_window *window = &display->windows[i];

            memset(window, 0, sizeof(*window));
            window->active = 1;
            window->id = (Window)(X11_COMPAT_FIRST_WINDOW + (unsigned long)i);
            window->x = x;
            window->y = y;
            window->width = width == 0u ? 1u : width;
            window->height = height == 0u ? 1u : height;
            window->background_pixel = (valuemask & CWBackPixel) && attributes != 0
                                     ? attributes->background_pixel
                                     : 0ul;
            window->border_pixel = (valuemask & CWBorderPixel) && attributes != 0
                                 ? attributes->border_pixel
                                 : 15ul;
            if ((valuemask & CWEventMask) && attributes != 0) {
                window->event_mask = attributes->event_mask;
            }
            return window->id;
        }
    }
    errno = ENOMEM;
    return None;
}

int XDestroyWindow(Display *display, Window window) {
    struct x11_compat_window *target = x11_find_window(display, window);
    XEvent event;

    if (target == 0) {
        errno = EINVAL;
        return 0;
    }
    x11_make_any_event(display, &event, DestroyNotify, window);
    (void)x11_queue_event(display, &event);
    memset(target, 0, sizeof(*target));
    return 1;
}

int XMapWindow(Display *display, Window window) {
    struct x11_compat_window *target = x11_find_window(display, window);

    if (target == 0) {
        errno = EINVAL;
        return 0;
    }
    target->mapped = 1;
    x11_fill_window_background(display, target);
    x11_queue_focus(display, target, FocusIn);
    x11_queue_expose(display, target);
    return 1;
}

int XUnmapWindow(Display *display, Window window) {
    struct x11_compat_window *target = x11_find_window(display, window);

    if (target == 0) {
        errno = EINVAL;
        return 0;
    }
    target->mapped = 0;
    x11_queue_focus(display, target, FocusOut);
    return 1;
}

int XMoveWindow(Display *display, Window window, int x, int y) {
    struct x11_compat_window *target = x11_find_window(display, window);

    if (target == 0) {
        errno = EINVAL;
        return 0;
    }
    target->x = x;
    target->y = y;
    if (target->mapped) {
        x11_fill_window_background(display, target);
        x11_queue_expose(display, target);
    }
    return 1;
}

int XResizeWindow(Display *display, Window window,
                  unsigned int width, unsigned int height) {
    struct x11_compat_window *target = x11_find_window(display, window);

    if (target == 0) {
        errno = EINVAL;
        return 0;
    }
    target->width = width == 0u ? 1u : width;
    target->height = height == 0u ? 1u : height;
    if (target->mapped) {
        x11_fill_window_background(display, target);
        x11_queue_expose(display, target);
    }
    return 1;
}

int XMoveResizeWindow(Display *display, Window window, int x, int y,
                      unsigned int width, unsigned int height) {
    if (!XMoveWindow(display, window, x, y)) {
        return 0;
    }
    return XResizeWindow(display, window, width, height);
}

int XStoreName(Display *display, Window window, const char *window_name) {
    struct x11_compat_window *target = x11_find_window(display, window);

    if (target == 0) {
        errno = EINVAL;
        return 0;
    }
    if (window_name == 0) {
        target->title[0] = '\0';
    } else {
        strncpy(target->title, window_name, X11_COMPAT_TITLE_MAX);
        target->title[X11_COMPAT_TITLE_MAX] = '\0';
    }
    if (target->mapped) {
        x11_fill_window_background(display, target);
    }
    return 1;
}

Pixmap XCreatePixmap(Display *display, Drawable drawable,
                     unsigned int width, unsigned int height,
                     unsigned int depth) {
    int i;

    (void)drawable;
    if (display == 0) {
        errno = EINVAL;
        return None;
    }
    for (i = 0; i < X11_COMPAT_MAX_PIXMAPS; ++i) {
        if (!display->pixmaps[i].active) {
            display->pixmaps[i].active = 1;
            display->pixmaps[i].id = (Pixmap)(X11_COMPAT_FIRST_PIXMAP + (unsigned long)i);
            display->pixmaps[i].width = width;
            display->pixmaps[i].height = height;
            display->pixmaps[i].depth = depth;
            return display->pixmaps[i].id;
        }
    }
    errno = ENOMEM;
    return None;
}

int XFreePixmap(Display *display, Pixmap pixmap) {
    struct x11_compat_pixmap *target = x11_find_pixmap(display, pixmap);

    if (target == 0) {
        errno = EINVAL;
        return 0;
    }
    memset(target, 0, sizeof(*target));
    return 1;
}

GC XCreateGC(Display *display, Drawable drawable,
             unsigned long valuemask, XGCValues *values) {
    GC gc;

    (void)display;
    (void)drawable;
    gc = (GC)calloc(1u, sizeof(*gc));
    if (gc == 0) {
        errno = ENOMEM;
        return 0;
    }
    gc->foreground = 15ul;
    gc->background = 0ul;
    gc->line_width = 1;
    if (values != 0) {
        if ((valuemask & GCForeground) != 0) {
            gc->foreground = values->foreground;
        }
        if ((valuemask & GCBackground) != 0) {
            gc->background = values->background;
        }
        if ((valuemask & GCLineWidth) != 0) {
            gc->line_width = values->line_width <= 0 ? 1 : values->line_width;
        }
    }
    return gc;
}

int XFreeGC(Display *display, GC gc) {
    (void)display;
    if (gc == 0) {
        errno = EINVAL;
        return 0;
    }
    free(gc);
    return 1;
}

int XSetForeground(Display *display, GC gc, unsigned long foreground) {
    (void)display;
    if (gc == 0) {
        errno = EINVAL;
        return 0;
    }
    gc->foreground = foreground;
    return 1;
}

int XSetBackground(Display *display, GC gc, unsigned long background) {
    (void)display;
    if (gc == 0) {
        errno = EINVAL;
        return 0;
    }
    gc->background = background;
    return 1;
}

int XSetLineAttributes(Display *display, GC gc, unsigned int line_width,
                       int line_style, int cap_style, int join_style) {
    (void)display;
    (void)line_style;
    (void)cap_style;
    (void)join_style;
    if (gc == 0) {
        errno = EINVAL;
        return 0;
    }
    gc->line_width = line_width == 0u ? 1 : (int)line_width;
    return 1;
}

int XDrawPoint(Display *display, Drawable drawable, GC gc, int x, int y) {
    struct x11_compat_window *window = x11_find_window(display, drawable);

    if (window == 0 || !window->mapped) {
        errno = EINVAL;
        return 0;
    }
    x11_rect(window->x + x, window->y + y, 1, 1, x11_color8(x11_gc_foreground(gc)));
    x11_present();
    return 1;
}

int XDrawLine(Display *display, Drawable drawable, GC gc,
              int x1, int y1, int x2, int y2) {
    struct x11_compat_window *window = x11_find_window(display, drawable);

    if (window == 0 || !window->mapped) {
        errno = EINVAL;
        return 0;
    }
    x11_draw_line_basic(window->x + x1,
                        window->y + y1,
                        window->x + x2,
                        window->y + y2,
                        x11_color8(x11_gc_foreground(gc)));
    x11_present();
    return 1;
}

int XDrawRectangle(Display *display, Drawable drawable, GC gc,
                   int x, int y, unsigned int width, unsigned int height) {
    if (!XDrawLine(display, drawable, gc, x, y, x + (int)width, y)) {
        return 0;
    }
    (void)XDrawLine(display, drawable, gc, x, y, x, y + (int)height);
    (void)XDrawLine(display, drawable, gc, x + (int)width, y, x + (int)width, y + (int)height);
    (void)XDrawLine(display, drawable, gc, x, y + (int)height, x + (int)width, y + (int)height);
    return 1;
}

int XFillRectangle(Display *display, Drawable drawable, GC gc,
                   int x, int y, unsigned int width, unsigned int height) {
    struct x11_compat_window *window = x11_find_window(display, drawable);

    if (window == 0 || !window->mapped) {
        errno = EINVAL;
        return 0;
    }
    x11_rect(window->x + x,
             window->y + y,
             (int)width,
             (int)height,
             x11_color8(x11_gc_foreground(gc)));
    x11_present();
    return 1;
}

int XDrawArc(Display *display, Drawable drawable, GC gc,
             int x, int y, unsigned int width, unsigned int height,
             int angle1, int angle2) {
    struct x11_compat_window *window = x11_find_window(display, drawable);

    (void)angle1;
    (void)angle2;
    if (window == 0 || !window->mapped) {
        errno = EINVAL;
        return 0;
    }
    x11_draw_arc_basic(0,
                       window->x + x,
                       window->y + y,
                       width,
                       height,
                       x11_color8(x11_gc_foreground(gc)));
    x11_present();
    return 1;
}

int XFillArc(Display *display, Drawable drawable, GC gc,
             int x, int y, unsigned int width, unsigned int height,
             int angle1, int angle2) {
    struct x11_compat_window *window = x11_find_window(display, drawable);

    (void)angle1;
    (void)angle2;
    if (window == 0 || !window->mapped) {
        errno = EINVAL;
        return 0;
    }
    x11_draw_arc_basic(1,
                       window->x + x,
                       window->y + y,
                       width,
                       height,
                       x11_color8(x11_gc_foreground(gc)));
    x11_present();
    return 1;
}

int XCopyArea(Display *display, Drawable src, Drawable dest, GC gc,
              int src_x, int src_y, unsigned int width, unsigned int height,
              int dest_x, int dest_y) {
    struct x11_compat_window *src_window = x11_find_window(display, src);
    struct x11_compat_window *dst_window = x11_find_window(display, dest);

    (void)src_x;
    (void)src_y;
    (void)gc;
    if (src_window == 0 || dst_window == 0 || !src_window->mapped || !dst_window->mapped) {
        errno = EINVAL;
        return 0;
    }
    x11_rect(dst_window->x + dest_x,
             dst_window->y + dest_y,
             (int)width,
             (int)height,
             (uint8_t)(src_window->background_pixel & 0xfful));
    x11_present();
    return 1;
}

XImage *XCreateImage(Display *display, Visual *visual, unsigned int depth,
                     int format, int offset, char *data, unsigned int width,
                     unsigned int height, int bitmap_pad,
                     int bytes_per_line) {
    XImage *image;

    (void)display;
    (void)visual;
    image = (XImage *)calloc(1u, sizeof(XImage));
    if (image == 0) {
        errno = ENOMEM;
        return 0;
    }
    image->width = (int)width;
    image->height = (int)height;
    image->xoffset = offset;
    image->format = format;
    image->data = data;
    image->bitmap_pad = bitmap_pad;
    image->depth = (int)depth;
    image->bytes_per_line = bytes_per_line > 0 ? bytes_per_line : (int)width;
    image->bits_per_pixel = depth <= 8u ? 8 : 32;
    return image;
}

int XDestroyImage(XImage *image) {
    if (image == 0) {
        errno = EINVAL;
        return 0;
    }
    free(image);
    return 1;
}

int XPutImage(Display *display, Drawable drawable, GC gc, XImage *image,
              int src_x, int src_y, int dest_x, int dest_y,
              unsigned int width, unsigned int height) {
    struct x11_compat_window *window = x11_find_window(display, drawable);
    unsigned int row;
    unsigned int col;

    (void)gc;
    if (window == 0 || image == 0 || image->data == 0 || !window->mapped) {
        errno = EINVAL;
        return 0;
    }
    for (row = 0; row < height; ++row) {
        const unsigned char *line = (const unsigned char *)image->data +
                                    (size_t)(src_y + (int)row) * (size_t)image->bytes_per_line;
        for (col = 0; col < width; ++col) {
            unsigned char color = line[src_x + (int)col];
            x11_rect(window->x + dest_x + (int)col,
                     window->y + dest_y + (int)row,
                     1,
                     1,
                     color);
        }
    }
    x11_present();
    return 1;
}

int XFlush(Display *display) {
    (void)display;
    x11_present();
    return 1;
}

int XSync(Display *display, Bool discard) {
    XEvent ignored;

    if (display == 0) {
        errno = EINVAL;
        return 0;
    }
    x11_present();
    if (discard) {
        while (x11_pop_event(display, &ignored, 1) != 0) {
        }
    }
    return 1;
}

Atom XInternAtom(Display *display, const char *atom_name, Bool only_if_exists) {
    (void)display;
    if (atom_name == 0) {
        errno = EINVAL;
        return None;
    }
    if (strcmp(atom_name, "WM_PROTOCOLS") == 0) {
        return 100ul;
    }
    if (strcmp(atom_name, "WM_DELETE_WINDOW") == 0) {
        return 101ul;
    }
    if (strcmp(atom_name, "UTF8_STRING") == 0) {
        return 102ul;
    }
    if (only_if_exists) {
        return None;
    }
    return (Atom)(103ul + (unsigned long)strlen(atom_name));
}

int XSetWMProtocols(Display *display, Window window, Atom *protocols,
                    int count) {
    (void)display;
    (void)window;
    (void)protocols;
    (void)count;
    return 1;
}

int XClearWindow(Display *display, Window window) {
    struct x11_compat_window *target = x11_find_window(display, window);

    if (target == 0 || !target->mapped) {
        errno = EINVAL;
        return 0;
    }
    x11_fill_window_background(display, target);
    x11_queue_expose(display, target);
    return 1;
}

void XFree(void *data) {
    free(data);
}

int XStringListToTextProperty(char **list, int count, XTextProperty *text_prop_return) {
    size_t total = 0u;
    unsigned char *buf;
    int i;
    size_t offset = 0u;

    if (text_prop_return == 0) {
        errno = EINVAL;
        return 0;
    }
    memset(text_prop_return, 0, sizeof(*text_prop_return));
    if (list == 0 || count <= 0) {
        return 1;
    }
    for (i = 0; i < count; ++i) {
        if (list[i] != 0) {
            total += strlen(list[i]) + 1u;
        }
    }
    buf = (unsigned char *)malloc(total == 0u ? 1u : total);
    if (buf == 0) {
        errno = ENOMEM;
        return 0;
    }
    for (i = 0; i < count; ++i) {
        size_t len;

        if (list[i] == 0) {
            continue;
        }
        len = strlen(list[i]);
        memcpy(buf + offset, list[i], len);
        offset += len;
        buf[offset++] = '\0';
    }
    text_prop_return->value = buf;
    text_prop_return->encoding = XA_STRING;
    text_prop_return->format = 8;
    text_prop_return->nitems = (unsigned long)offset;
    return 1;
}

int XSetWMName(Display *display, Window window, XTextProperty *text_prop) {
    if (text_prop == 0 || text_prop->value == 0) {
        errno = EINVAL;
        return 0;
    }
    return XStoreName(display, window, (const char *)text_prop->value);
}

int XSetWMIconName(Display *display, Window window, XTextProperty *text_prop) {
    return XSetWMName(display, window, text_prop);
}

int XSetWMHints(Display *display, Window window, XWMHints *wm_hints) {
    (void)display;
    (void)window;
    (void)wm_hints;
    return 1;
}

int XSetClassHint(Display *display, Window window, XClassHint *class_hints) {
    (void)display;
    (void)window;
    (void)class_hints;
    return 1;
}

int XLookupString(XKeyEvent *event_struct, char *buffer_return, int bytes_buffer,
                  KeySym *keysym_return, XComposeStatus *status_in_out) {
    unsigned int keycode;

    (void)status_in_out;
    if (event_struct == 0) {
        errno = EINVAL;
        return 0;
    }
    keycode = event_struct->keycode;
    if (keysym_return != 0) {
        *keysym_return = (KeySym)keycode;
    }
    if (buffer_return != 0 && bytes_buffer > 0) {
        if (keycode < 0x80u) {
            buffer_return[0] = (char)keycode;
            return 1;
        }
        buffer_return[0] = '\0';
    }
    return 0;
}
