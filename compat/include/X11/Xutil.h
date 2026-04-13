#ifndef VIBE_COMPAT_X11_XUTIL_H
#define VIBE_COMPAT_X11_XUTIL_H

#include <X11/Xlib.h>

typedef struct {
    unsigned char *value;
    Atom encoding;
    int format;
    unsigned long nitems;
} XTextProperty;

typedef struct {
    long flags;
    int x;
    int y;
    int width;
    int height;
    int min_width;
    int min_height;
    int max_width;
    int max_height;
} XSizeHints;

typedef struct {
    char *res_name;
    char *res_class;
} XClassHint;

typedef struct {
    long flags;
    Bool input;
    int initial_state;
    Pixmap icon_pixmap;
    Window icon_window;
    int icon_x;
    int icon_y;
    Pixmap icon_mask;
    XID window_group;
} XWMHints;

typedef struct {
    int line;
    int chars_matched;
} XComposeStatus;

#define PPosition (1L << 2)
#define PSize (1L << 3)
#define PMinSize (1L << 4)
#define PMaxSize (1L << 5)

#define InputHint (1L << 0)
#define StateHint (1L << 1)
#define IconPixmapHint (1L << 2)

#define NormalState 1

#define XStringStyle 0
#define XStdICCTextStyle 1
#define XUTF8StringStyle 2

int XStringListToTextProperty(char **list, int count, XTextProperty *text_prop_return);
int XSetWMName(Display *display, Window window, XTextProperty *text_prop);
int XSetWMIconName(Display *display, Window window, XTextProperty *text_prop);
int XSetWMHints(Display *display, Window window, XWMHints *wm_hints);
int XSetClassHint(Display *display, Window window, XClassHint *class_hints);
int XLookupString(XKeyEvent *event_struct, char *buffer_return, int bytes_buffer,
                  KeySym *keysym_return, XComposeStatus *status_in_out);

#endif
