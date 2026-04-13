#ifndef VIBE_SYS_IOCTL_H
#define VIBE_SYS_IOCTL_H

#include <stdarg.h>
#include <errno.h>
#include <include/userland_api.h>
#include <sys/ioccom.h>
#include <sys/termios.h>

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

#define FIOCLEX   _IO('f', 1)
#define FIONCLEX  _IO('f', 2)
#define FIONREAD  _IOR('f', 127, int)
#define FIONBIO   _IOW('f', 126, int)
#define FIOASYNC  _IOW('f', 125, int)
#define FIOSETOWN _IOW('f', 124, int)
#define FIOGETOWN _IOR('f', 123, int)

#define TIOCGETA   _IOR('t', 19, struct termios)
#define TIOCSETA   _IOW('t', 20, struct termios)
#define TIOCSETAW  _IOW('t', 21, struct termios)
#define TIOCSETAF  _IOW('t', 22, struct termios)
#define TIOCGPGRP  _IOR('t', 119, int)
#define TIOCSPGRP  _IOW('t', 118, int)
#define TIOCGWINSZ _IOR('t', 104, struct winsize)
#define TIOCSWINSZ _IOW('t', 103, struct winsize)

static inline int vibe_ioctl_syscall(int num, int a, int b, int c, int d, int e) {
    int ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                     : "memory", "cc");
    return ret;
}

static inline void vibe_ioctl_default_winsize(struct winsize *ws) {
    struct video_mode mode;
    int cols = 80;
    int rows = 25;

    if (ws == 0) {
        return;
    }

    ws->ws_row = 25;
    ws->ws_col = 80;
    ws->ws_xpixel = 0;
    ws->ws_ypixel = 0;

    if (vibe_ioctl_syscall(SYSCALL_GFX_INFO, (int)(uintptr_t)&mode, 0, 0, 0, 0) == 0 &&
        mode.width != 0u && mode.height != 0u) {
        cols = (int)(mode.width / 8u);
        rows = (int)(mode.height / 16u);
        if (cols > 0) {
            ws->ws_col = (unsigned short)cols;
        }
        if (rows > 0) {
            ws->ws_row = (unsigned short)rows;
        }
        ws->ws_xpixel = (unsigned short)mode.width;
        ws->ws_ypixel = (unsigned short)mode.height;
    }
}

static inline int ioctl(int fd, unsigned long request, ...) {
    va_list ap;
    void *arg;

    if (fd < 0) {
        errno = EBADF;
        return -1;
    }

    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);

    switch (request) {
    case FIOCLEX:
    case FIONCLEX:
    case FIONBIO:
    case FIOASYNC:
    case FIOSETOWN:
    case TIOCSETA:
    case TIOCSETAW:
    case TIOCSETAF:
    case TIOCSPGRP:
    case TIOCSWINSZ:
        return 0;
    case FIONREAD:
    case FIOGETOWN:
    case TIOCGPGRP:
        if (arg == 0) {
            errno = EFAULT;
            return -1;
        }
        *(int *)arg = 0;
        return 0;
    case TIOCGETA:
        if (arg == 0) {
            errno = EFAULT;
            return -1;
        }
        vibe_termios_make_default((struct termios *)arg);
        return 0;
    case TIOCGWINSZ:
        if (arg == 0) {
            errno = EFAULT;
            return -1;
        }
        vibe_ioctl_default_winsize((struct winsize *)arg);
        return 0;
    default:
        errno = ENOTTY;
        return -1;
    }
}

#endif
