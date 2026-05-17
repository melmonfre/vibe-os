#ifndef COMPAT_SYS_IOCTL_H
#define COMPAT_SYS_IOCTL_H

#include <stdarg.h>
#include <net/if.h>
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

#define SIOCGIFFLAGS   _IOWR('i', 17, struct ifreq)
#define SIOCSIFFLAGS   _IOW('i', 16, struct ifreq)
#define SIOCGIFADDR    _IOWR('i', 33, struct ifreq)
#define SIOCGIFDSTADDR _IOWR('i', 34, struct ifreq)
#define SIOCGIFBRDADDR _IOWR('i', 35, struct ifreq)
#define SIOCGIFCONF    _IOWR('i', 36, struct ifconf)
#define SIOCGIFNETMASK _IOWR('i', 37, struct ifreq)

int ioctl(int fd, unsigned long request, ...);

#endif
