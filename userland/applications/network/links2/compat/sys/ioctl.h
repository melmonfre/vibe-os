#ifndef SYS_IOCTL_H
#define SYS_IOCTL_H
extern int ioctl(int, unsigned long, ...);
#define TIOCGWINSZ 0x5413

/* links2's get_terminal_size() declares `struct winsize ws` under
 * TIOCGWINSZ; the layout is the POSIX/Linux one. */
struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};
#define TIOCGETA 0x5401
#define TIOCSETA 0x5402
#define FIONREAD 0x541B
#endif
