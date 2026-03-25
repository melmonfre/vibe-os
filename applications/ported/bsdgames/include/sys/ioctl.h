#ifndef VIBE_BSDGAME_SYS_IOCTL_H
#define VIBE_BSDGAME_SYS_IOCTL_H

#include <compat_defs.h>

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

#define TIOCGWINSZ 0x5413

int ioctl(int fd, unsigned long request, ...);

#endif
