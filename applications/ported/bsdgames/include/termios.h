#ifndef VIBE_BSDGAME_TERMIOS_H
#define VIBE_BSDGAME_TERMIOS_H

typedef unsigned int speed_t;
typedef unsigned int tcflag_t;
typedef unsigned char cc_t;

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_cc[32];
    speed_t c_ispeed;
    speed_t c_ospeed;
};

#define B9600 9600u
#define ICANON 0x0002u
#define ECHO 0x0008u
#define ICRNL 0x0100u
#define OXTABS 0x0004u
#define VERASE 2
#define VKILL 3
#define VSUSP 10
#define VMIN 6
#define VTIME 5
#define _POSIX_VDISABLE ((cc_t)0xffu)
#define TCSADRAIN 1
#define TCIFLUSH 0

int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
int tcflush(int fd, int queue_selector);
speed_t baudrate(void);

int tcgetattr(int fd, struct termios *termios_p);
speed_t cfgetospeed(const struct termios *termios_p);

#endif
