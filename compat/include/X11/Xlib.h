#ifndef VIBE_COMPAT_X11_XLIB_H
#define VIBE_COMPAT_X11_XLIB_H

#include <X11/X.h>

struct _XDisplay;
struct _XGC;
struct _XIC;
struct _XIM;

typedef struct _XDisplay Display;
typedef struct _XGC *GC;
typedef struct _XIC *XIC;
typedef struct _XIM *XIM;

typedef struct {
    void *ext_data;
    VisualID visualid;
    int class;
    unsigned long red_mask;
    unsigned long green_mask;
    unsigned long blue_mask;
    int bits_per_rgb;
    int map_entries;
} Visual;

typedef struct {
    int depth;
    int bits_per_pixel;
    int scanline_pad;
} XPixmapFormatValues;

typedef struct {
    int x;
    int y;
    unsigned int width;
    unsigned int height;
} XRectangle;

typedef struct {
    unsigned long pixel;
    unsigned short red;
    unsigned short green;
    unsigned short blue;
    char flags;
    char pad;
} XColor;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
} XAnyEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    Window root;
    Window subwindow;
    Time time;
    int x;
    int y;
    int x_root;
    int y_root;
    unsigned int state;
    unsigned int keycode;
    Bool same_screen;
} XKeyEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    Window root;
    Window subwindow;
    Time time;
    int x;
    int y;
    int x_root;
    int y_root;
    unsigned int state;
    unsigned int button;
    Bool same_screen;
} XButtonEvent;

typedef XButtonEvent XMotionEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    int x;
    int y;
    int width;
    int height;
    int count;
} XExposeEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    int mode;
    int detail;
} XFocusChangeEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    Atom message_type;
    int format;
    union {
        char b[20];
        short s[10];
        long l[5];
    } data;
} XClientMessageEvent;

typedef union _XEvent {
    int type;
    XAnyEvent xany;
    XKeyEvent xkey;
    XButtonEvent xbutton;
    XMotionEvent xmotion;
    XExposeEvent xexpose;
    XFocusChangeEvent xfocus;
    XClientMessageEvent xclient;
    long pad[24];
} XEvent;

typedef struct {
    long background_pixmap;
    unsigned long background_pixel;
    unsigned long border_pixel;
    Colormap colormap;
    long event_mask;
} XSetWindowAttributes;

typedef struct {
    int function;
    unsigned long foreground;
    unsigned long background;
    int line_width;
} XGCValues;

typedef struct _XImage {
    int width;
    int height;
    int xoffset;
    int format;
    char *data;
    int byte_order;
    int bitmap_unit;
    int bitmap_bit_order;
    int bitmap_pad;
    int depth;
    int bytes_per_line;
    int bits_per_pixel;
    unsigned long red_mask;
    unsigned long green_mask;
    unsigned long blue_mask;
} XImage;

Display *XOpenDisplay(const char *display_name);
int XCloseDisplay(Display *display);
int XConnectionNumber(Display *display);
int DefaultScreen(Display *display);
Window RootWindow(Display *display, int screen_number);
Colormap DefaultColormap(Display *display, int screen_number);
unsigned long BlackPixel(Display *display, int screen_number);
unsigned long WhitePixel(Display *display, int screen_number);
int DisplayWidth(Display *display, int screen_number);
int DisplayHeight(Display *display, int screen_number);
int XPending(Display *display);
int XNextEvent(Display *display, XEvent *event_return);
int XPeekEvent(Display *display, XEvent *event_return);
int XSelectInput(Display *display, Window window, long event_mask);
Window XCreateSimpleWindow(Display *display, Window parent, int x, int y,
                           unsigned int width, unsigned int height,
                           unsigned int border_width, unsigned long border,
                           unsigned long background);
Window XCreateWindow(Display *display, Window parent, int x, int y,
                     unsigned int width, unsigned int height,
                     unsigned int border_width, int depth,
                     unsigned int class, Visual *visual,
                     unsigned long valuemask,
                     XSetWindowAttributes *attributes);
int XDestroyWindow(Display *display, Window window);
int XMapWindow(Display *display, Window window);
int XUnmapWindow(Display *display, Window window);
int XMoveWindow(Display *display, Window window, int x, int y);
int XResizeWindow(Display *display, Window window,
                  unsigned int width, unsigned int height);
int XMoveResizeWindow(Display *display, Window window, int x, int y,
                      unsigned int width, unsigned int height);
int XStoreName(Display *display, Window window, const char *window_name);
Pixmap XCreatePixmap(Display *display, Drawable drawable,
                     unsigned int width, unsigned int height,
                     unsigned int depth);
int XFreePixmap(Display *display, Pixmap pixmap);
GC XCreateGC(Display *display, Drawable drawable,
             unsigned long valuemask, XGCValues *values);
int XFreeGC(Display *display, GC gc);
int XSetForeground(Display *display, GC gc, unsigned long foreground);
int XSetBackground(Display *display, GC gc, unsigned long background);
int XSetLineAttributes(Display *display, GC gc, unsigned int line_width,
                       int line_style, int cap_style, int join_style);
int XDrawPoint(Display *display, Drawable drawable, GC gc, int x, int y);
int XDrawLine(Display *display, Drawable drawable, GC gc,
              int x1, int y1, int x2, int y2);
int XDrawRectangle(Display *display, Drawable drawable, GC gc,
                   int x, int y, unsigned int width, unsigned int height);
int XFillRectangle(Display *display, Drawable drawable, GC gc,
                   int x, int y, unsigned int width, unsigned int height);
int XDrawArc(Display *display, Drawable drawable, GC gc,
             int x, int y, unsigned int width, unsigned int height,
             int angle1, int angle2);
int XFillArc(Display *display, Drawable drawable, GC gc,
             int x, int y, unsigned int width, unsigned int height,
             int angle1, int angle2);
int XCopyArea(Display *display, Drawable src, Drawable dest, GC gc,
              int src_x, int src_y, unsigned int width, unsigned int height,
              int dest_x, int dest_y);
XImage *XCreateImage(Display *display, Visual *visual, unsigned int depth,
                     int format, int offset, char *data, unsigned int width,
                     unsigned int height, int bitmap_pad,
                     int bytes_per_line);
int XDestroyImage(XImage *image);
int XPutImage(Display *display, Drawable drawable, GC gc, XImage *image,
              int src_x, int src_y, int dest_x, int dest_y,
              unsigned int width, unsigned int height);
int XFlush(Display *display);
int XSync(Display *display, Bool discard);
Atom XInternAtom(Display *display, const char *atom_name, Bool only_if_exists);
int XSetWMProtocols(Display *display, Window window, Atom *protocols,
                    int count);
int XClearWindow(Display *display, Window window);
void XFree(void *data);

#define ConnectionNumber(display) XConnectionNumber(display)
#define DefaultRootWindow(display) RootWindow((display), DefaultScreen((display)))

#endif
