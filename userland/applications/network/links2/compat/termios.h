#ifndef TERMIOS_H
#define TERMIOS_H
#include <sys/types.h>
typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;
#define NCCS 32
struct termios {
	tcflag_t c_iflag;
	tcflag_t c_oflag;
	tcflag_t c_cflag;
	tcflag_t c_lflag;
	cc_t c_cc[NCCS];
};
extern int tcgetattr(int, struct termios *);
extern int tcsetattr(int, int, const struct termios *);
#define TCSANOW 0
#define TCSADRAIN 1
#define TCSAFLUSH 2
#define ECHO 0000010
#define ICANON 0000002
#define ISIG 0000001
#define ICRNL 0000400
#define OPOST 0000001
#define ONLCR 0000004
#define VMIN 6
#define VTIME 5
#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VEOL 11

/* Standard Linux/i386 termios constants. The skeleton header shipped with
 * the port declared only the handful kbd.c's os_cfmakeraw() first touched,
 * and links2 uses the full manual set (HAVE_CFMAKERAW is off). The values
 * are the canonical asm-generic ones; the compat library's tcsetattr()
 * maps them onto the kernel tty path. */
/* c_iflag */
#define IGNBRK 0000001
#define BRKINT 0000002
#define IGNPAR 0000004
#define PARMRK 0000010
#define INPCK 0000020
#define ISTRIP 0000040
#define INLCR 0000100
#define IGNCR 0000200
#define IUCLC 0001000
#define IXON 0002000
#define IXANY 0004000
#define IXOFF 0010000
#define IMAXBEL 0020000
#define IUTF8 0040000
/* c_oflag */
#define OLCUC 0000002
#define OCRNL 0000010
#define ONOCR 0000020
#define ONLRET 0000040
#define OFILL 0000100
#define OFDEL 0000200
#define NLDLY 0000400
#define NL0 0000000
#define NL1 0000400
#define CRDLY 0003000
#define CR0 0000000
#define CR1 0001000
#define CR2 0002000
#define CR3 0003000
#define TABDLY 0014000
#define TAB0 0000000
#define TAB1 0004000
#define TAB2 0010000
#define TAB3 0014000
#define BSDLY 0020000
#define BS0 0000000
#define BS1 0020000
#define FFDLY 0040000
#define FF0 0000000
#define FF1 0040000
/* c_cflag */
#define CSIZE 0000060
#define CS5 0000000
#define CS6 0000020
#define CS7 0000040
#define CS8 0000060
#define CSTOPB 0000100
#define CREAD 0000200
#define PARENB 0000400
#define PARODD 0001000
#define HUPCL 0002000
#define CLOCAL 0004000
#define CBAUD 0010017
#define B0 0010000
#define B1200 0000013
#define B2400 0000014
#define B4800 0000015
#define B9600 0000016
#define B19200 0000017
#define EXTA 0000016
#define EXTB 0000017
/* c_lflag */
#define ECHOE 0000020
#define ECHOK 0000040
#define ECHONL 0000100
#define NOFLSH 0000200
#define TOSTOP 0000040
#define ECHOCTL 0001000
#define ECHOPRT 0002000
#define ECHOKE 0004000
#define FLUSHO 0010000
#define PENDIN 0040000
#define IEXTEN 0100000
/* c_cc indexes */
#define VSWTC 7
#define VSTART 8
#define VSTOP 9
#define VSUSP 10
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE 14
#define VLNEXT 15
#define VEOL2 16
#endif
