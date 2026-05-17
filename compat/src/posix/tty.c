#include "compat_tty_input.h"

#include "compat_fd_state.h"

#include <stdint.h>

#include <compat/posix/errno.h>
#include <compat/posix/fcntl.h>
#include <compat/posix/unistd.h>
#include <compat/libc/string.h>
#include <headers/signal.h>
#include <include/userland_api.h>
#include <kernel/microkernel/network.h>
#include <lang/include/vibe_app_runtime.h>
#include <net/if.h>
#include <netinet/in.h>

#define COMPAT_TTY_CANON_BUF 256

static char g_compat_tty_canon_buf[COMPAT_TTY_CANON_BUF];
static size_t g_compat_tty_canon_len = 0u;
static size_t g_compat_tty_canon_pos = 0u;

static int compat_syscall5(int num, int a, int b, int c, int d, int e) {
    int ret;

    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                     : "memory", "cc");
    return ret;
}

static void compat_refresh_winsize(struct winsize *ws) {
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

    if (compat_syscall5(SYSCALL_GFX_INFO, (int)(uintptr_t)&mode, 0, 0, 0, 0) == 0 &&
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

static int compat_tty_echo_char(const struct termios *termios_state, int c) {
    if (termios_state == 0 || (termios_state->c_lflag & ECHO) == 0) {
        return 0;
    }
    if (c == '\n') {
        vibe_app_console_putc('\n');
        return 0;
    }
    if ((termios_state->c_lflag & ECHOCTL) != 0 &&
        c > 0 && c < 32 && c != '\t') {
        vibe_app_console_putc('^');
        vibe_app_console_putc((char)(c + '@'));
        return 0;
    }
    vibe_app_console_putc((char)c);
    return 0;
}

static int compat_tty_echo_backspace(const struct termios *termios_state) {
    if (termios_state == 0 || (termios_state->c_lflag & ECHO) == 0) {
        return 0;
    }
    vibe_app_console_putc('\b');
    vibe_app_console_putc(' ');
    vibe_app_console_putc('\b');
    return 0;
}

static int compat_tty_signal_char(const struct termios *termios_state, int c) {
    if (termios_state == 0 || (termios_state->c_lflag & ISIG) == 0) {
        return 0;
    }
    if (termios_state->c_cc[VINTR] != _POSIX_VDISABLE &&
        c == termios_state->c_cc[VINTR]) {
        (void)raise(SIGINT);
        return 1;
    }
    if (termios_state->c_cc[VQUIT] != _POSIX_VDISABLE &&
        c == termios_state->c_cc[VQUIT]) {
        (void)raise(SIGQUIT);
        return 1;
    }
    if (termios_state->c_cc[VSUSP] != _POSIX_VDISABLE &&
        c == termios_state->c_cc[VSUSP]) {
        (void)raise(SIGTSTP);
        return 1;
    }
    return 0;
}

static int compat_tty_translate_input(const struct termios *termios_state, int c) {
    if (termios_state == 0) {
        return c;
    }
    if ((termios_state->c_iflag & ICRNL) != 0 && c == '\r') {
        return '\n';
    }
    if ((termios_state->c_iflag & INLCR) != 0 && c == '\n') {
        return '\r';
    }
    if ((termios_state->c_iflag & IGNCR) != 0 && c == '\r') {
        return -1;
    }
    return c;
}

static int compat_tty_poll_char(void) {
    int c;

    for (;;) {
        c = vibe_app_poll_key();
        if (c > 0) {
            return c;
        }
        vibe_app_yield();
    }
}

static ssize_t compat_tty_consume_canon(void *buf, size_t count) {
    size_t available;
    size_t copy_len;

    if (g_compat_tty_canon_pos >= g_compat_tty_canon_len) {
        g_compat_tty_canon_pos = 0u;
        g_compat_tty_canon_len = 0u;
        return 0;
    }

    available = g_compat_tty_canon_len - g_compat_tty_canon_pos;
    copy_len = count < available ? count : available;
    memcpy(buf, g_compat_tty_canon_buf + g_compat_tty_canon_pos, copy_len);
    g_compat_tty_canon_pos += copy_len;
    if (g_compat_tty_canon_pos >= g_compat_tty_canon_len) {
        g_compat_tty_canon_pos = 0u;
        g_compat_tty_canon_len = 0u;
    }
    return (ssize_t)copy_len;
}

static ssize_t compat_tty_read_canonical(const struct termios *termios_state, void *buf, size_t count) {
    ssize_t copied;

    copied = compat_tty_consume_canon(buf, count);
    if (copied > 0) {
        return copied;
    }

    g_compat_tty_canon_pos = 0u;
    g_compat_tty_canon_len = 0u;

    for (;;) {
        int c = compat_tty_poll_char();

        c = compat_tty_translate_input(termios_state, c);
        if (c < 0) {
            continue;
        }
        if (compat_tty_signal_char(termios_state, c) != 0) {
            errno = EINTR;
            return -1;
        }
        if (termios_state->c_cc[VERASE] != _POSIX_VDISABLE &&
            c == termios_state->c_cc[VERASE]) {
            if (g_compat_tty_canon_len > 0u) {
                g_compat_tty_canon_len -= 1u;
                if ((termios_state->c_lflag & ECHOE) != 0 ||
                    (termios_state->c_lflag & ECHO) != 0) {
                    (void)compat_tty_echo_backspace(termios_state);
                }
            }
            continue;
        }
        if (termios_state->c_cc[VKILL] != _POSIX_VDISABLE &&
            c == termios_state->c_cc[VKILL]) {
            while (g_compat_tty_canon_len > 0u) {
                g_compat_tty_canon_len -= 1u;
                if ((termios_state->c_lflag & ECHOK) != 0 ||
                    (termios_state->c_lflag & ECHO) != 0) {
                    (void)compat_tty_echo_backspace(termios_state);
                }
            }
            continue;
        }
        if (termios_state->c_cc[VEOF] != _POSIX_VDISABLE &&
            c == termios_state->c_cc[VEOF]) {
            if (g_compat_tty_canon_len == 0u) {
                return 0;
            }
            break;
        }

        if (g_compat_tty_canon_len + 1u < COMPAT_TTY_CANON_BUF) {
            g_compat_tty_canon_buf[g_compat_tty_canon_len++] = (char)c;
            (void)compat_tty_echo_char(termios_state, c);
        }

        if (c == '\n' ||
            (termios_state->c_cc[VEOL] != _POSIX_VDISABLE && c == termios_state->c_cc[VEOL]) ||
            (termios_state->c_cc[VEOL2] != _POSIX_VDISABLE && c == termios_state->c_cc[VEOL2])) {
            break;
        }
    }

    return compat_tty_consume_canon(buf, count);
}

static ssize_t compat_tty_read_raw(const struct termios *termios_state, void *buf, size_t count) {
    size_t need = 1u;
    size_t got = 0u;

    if (termios_state != 0 &&
        termios_state->c_cc[VMIN] != _POSIX_VDISABLE &&
        termios_state->c_cc[VMIN] > 0) {
        need = termios_state->c_cc[VMIN];
        if (need > count) {
            need = count;
        }
    }

    while (got < need) {
        int c = compat_tty_poll_char();

        c = compat_tty_translate_input(termios_state, c);
        if (c < 0) {
            continue;
        }
        if (compat_tty_signal_char(termios_state, c) != 0) {
            errno = EINTR;
            return got > 0u ? (ssize_t)got : -1;
        }
        ((char *)buf)[got++] = (char)c;
        (void)compat_tty_echo_char(termios_state, c);
        if (got >= count) {
            break;
        }
    }

    return (ssize_t)got;
}

ssize_t compat_tty_read(int fd, void *buf, size_t count) {
    const struct compat_fd_entry *entry = compat_fd_get_const(fd);

    if (buf == 0) {
        errno = EINVAL;
        return -1;
    }
    if (count == 0u) {
        return 0;
    }
    if (entry == 0 || !entry->is_tty) {
        errno = EBADF;
        return -1;
    }
    if ((entry->termios_state.c_lflag & ICANON) != 0) {
        return compat_tty_read_canonical(&entry->termios_state, buf, count);
    }
    return compat_tty_read_raw(&entry->termios_state, buf, count);
}

static int compat_network_status(struct mk_network_status *status) {
    if (status == 0) {
        return -1;
    }
    memset(status, 0, sizeof(*status));
    return compat_syscall5(SYSCALL_NETWORK_GET_STATUS,
                           (int)(uintptr_t)status,
                           0,
                           0,
                           0,
                           0);
}

static int compat_parse_ipv4_string(const char *text, uint32_t *out_addr) {
    uint32_t parts[4];
    uint32_t value = 0u;
    int index = 0;
    int digit_seen = 0;

    if (text == 0 || out_addr == 0) {
        return -1;
    }

    memset(parts, 0, sizeof(parts));
    while (*text != '\0') {
        if (*text >= '0' && *text <= '9') {
            value = (value * 10u) + (uint32_t)(*text - '0');
            if (value > 255u) {
                return -1;
            }
            digit_seen = 1;
        } else if (*text == '.') {
            if (!digit_seen || index >= 3) {
                return -1;
            }
            parts[index++] = value;
            value = 0u;
            digit_seen = 0;
        } else {
            return -1;
        }
        ++text;
    }
    if (!digit_seen || index != 3) {
        return -1;
    }
    parts[index] = value;
    *out_addr = (parts[0] << 24) |
                (parts[1] << 16) |
                (parts[2] << 8) |
                parts[3];
    return 0;
}

static void compat_set_sockaddr_ipv4(struct sockaddr *dst, uint32_t host_addr) {
    struct sockaddr_in *sin = (struct sockaddr_in *)dst;

    memset(dst, 0, sizeof(struct sockaddr_in));
    sin->sin_len = (uint8_t)sizeof(struct sockaddr_in);
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = htonl(host_addr);
}

static int compat_network_match_interface(const char *name,
                                          const struct mk_network_status *status,
                                          int *is_loopback) {
    if (name == 0 || name[0] == '\0') {
        return -1;
    }
    if (strcmp(name, "lo0") == 0) {
        if (is_loopback != 0) {
            *is_loopback = 1;
        }
        return 0;
    }
    if (status != 0 &&
        status->active_if[0] != '\0' &&
        strcmp(name, status->active_if) == 0) {
        if (is_loopback != 0) {
            *is_loopback = 0;
        }
        return 0;
    }
    errno = ENXIO;
    return -1;
}

static short compat_network_interface_flags(const struct mk_network_status *status, int is_loopback) {
    short flags = (short)(IFF_UP | IFF_RUNNING | IFF_MULTICAST);

    if (is_loopback) {
        flags |= IFF_LOOPBACK;
        return flags;
    }
    flags |= (short)(IFF_BROADCAST | IFF_SIMPLEX);
    if (status == 0 || status->link_state == MK_NETWORK_LINK_DISCONNECTED) {
        flags &= (short)~IFF_RUNNING;
    }
    return flags;
}

static int compat_network_fill_addr(struct ifreq *ifr,
                                    const struct mk_network_status *status,
                                    unsigned long request,
                                    int is_loopback) {
    uint32_t host_addr = 0u;

    if (ifr == 0) {
        errno = EFAULT;
        return -1;
    }

    switch (request) {
    case SIOCGIFADDR:
        if (is_loopback) {
            host_addr = 0x7f000001u;
            break;
        }
        if (status == 0 || status->ip_address[0] == '\0' ||
            compat_parse_ipv4_string(status->ip_address, &host_addr) != 0) {
            errno = EADDRNOTAVAIL;
            return -1;
        }
        break;
    case SIOCGIFNETMASK:
        host_addr = is_loopback ? 0xff000000u : 0xffffff00u;
        break;
    case SIOCGIFBRDADDR:
        if (is_loopback) {
            host_addr = 0x7f000001u;
            break;
        }
        if (status == 0 || status->ip_address[0] == '\0' ||
            compat_parse_ipv4_string(status->ip_address, &host_addr) != 0) {
            errno = EADDRNOTAVAIL;
            return -1;
        }
        host_addr |= 0x000000ffu;
        break;
    case SIOCGIFDSTADDR:
        if (is_loopback) {
            host_addr = 0x7f000001u;
            break;
        }
        if (status == 0 || status->gateway[0] == '\0' ||
            compat_parse_ipv4_string(status->gateway, &host_addr) != 0) {
            errno = EADDRNOTAVAIL;
            return -1;
        }
        break;
    default:
        errno = EINVAL;
        return -1;
    }

    switch (request) {
    case SIOCGIFDSTADDR:
        compat_set_sockaddr_ipv4(&ifr->ifr_dstaddr, host_addr);
        break;
    case SIOCGIFBRDADDR:
        compat_set_sockaddr_ipv4(&ifr->ifr_broadaddr, host_addr);
        break;
    default:
        compat_set_sockaddr_ipv4(&ifr->ifr_addr, host_addr);
        break;
    }
    return 0;
}

static int compat_network_fill_ifconf(struct ifconf *ifc, const struct mk_network_status *status) {
    int count = 1;
    int capacity;
    int written = 0;
    struct ifreq *req;

    if (ifc == 0 || ifc->ifc_buf == 0) {
        errno = EFAULT;
        return -1;
    }

    if (status != 0 &&
        status->active_if[0] != '\0' &&
        strcmp(status->active_if, "lo0") != 0) {
        count += 1;
    }

    capacity = ifc->ifc_len / (int)sizeof(struct ifreq);
    req = ifc->ifc_req;
    if (capacity <= 0) {
        ifc->ifc_len = 0;
        return 0;
    }

    memset(req, 0, (size_t)capacity * sizeof(struct ifreq));

    strncpy(req[written].ifr_name, "lo0", sizeof(req[written].ifr_name) - 1u);
    compat_set_sockaddr_ipv4(&req[written].ifr_addr, 0x7f000001u);
    written += 1;

    if (written < capacity &&
        count > 1 &&
        status != 0 &&
        status->active_if[0] != '\0') {
        strncpy(req[written].ifr_name, status->active_if, sizeof(req[written].ifr_name) - 1u);
        if (status->ip_address[0] != '\0') {
            uint32_t host_addr = 0u;

            if (compat_parse_ipv4_string(status->ip_address, &host_addr) == 0) {
                compat_set_sockaddr_ipv4(&req[written].ifr_addr, host_addr);
            }
        }
        written += 1;
    }

    ifc->ifc_len = written * (int)sizeof(struct ifreq);
    return 0;
}

speed_t cfgetispeed(const struct termios *t) {
    return t ? (speed_t)t->c_ispeed : 0;
}

speed_t cfgetospeed(const struct termios *t) {
    return t ? (speed_t)t->c_ospeed : 0;
}

int cfsetispeed(struct termios *t, speed_t speed) {
    if (t == 0) {
        errno = EINVAL;
        return -1;
    }
    t->c_ispeed = (int)speed;
    return 0;
}

int cfsetospeed(struct termios *t, speed_t speed) {
    if (t == 0) {
        errno = EINVAL;
        return -1;
    }
    t->c_ospeed = (int)speed;
    return 0;
}

int cfsetspeed(struct termios *t, speed_t speed) {
    if (cfsetispeed(t, speed) != 0) {
        return -1;
    }
    return cfsetospeed(t, speed);
}

void cfmakeraw(struct termios *t) {
    if (t == 0) {
        return;
    }
    t->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    t->c_oflag &= ~OPOST;
    t->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t->c_cflag &= ~(CSIZE | PARENB);
    t->c_cflag |= CS8;
    t->c_cc[VMIN] = 1;
    t->c_cc[VTIME] = 0;
}

int tcgetattr(int fd, struct termios *t) {
    const struct compat_fd_entry *entry;

    if (t == 0) {
        errno = EINVAL;
        return -1;
    }

    entry = compat_fd_get_const(fd);
    if (entry == 0) {
        errno = EBADF;
        return -1;
    }
    if (!entry->is_tty) {
        errno = ENOTTY;
        return -1;
    }

    *t = entry->termios_state;
    return 0;
}

int tcsetattr(int fd, int action, const struct termios *t) {
    struct compat_fd_entry *entry;

    if (t == 0) {
        errno = EINVAL;
        return -1;
    }
    if (action != TCSANOW && action != TCSADRAIN && action != TCSAFLUSH &&
        action != (TCSANOW | TCSASOFT) &&
        action != (TCSADRAIN | TCSASOFT) &&
        action != (TCSAFLUSH | TCSASOFT)) {
        errno = EINVAL;
        return -1;
    }

    entry = compat_fd_get(fd);
    if (entry == 0) {
        errno = EBADF;
        return -1;
    }
    if (!entry->is_tty) {
        errno = ENOTTY;
        return -1;
    }

    entry->termios_state = *t;
    return 0;
}

int tcdrain(int fd) {
    if (!compat_fd_is_valid(fd)) {
        errno = EBADF;
        return -1;
    }
    return 0;
}

int tcflow(int fd, int action) {
    if (!compat_fd_is_valid(fd)) {
        errno = EBADF;
        return -1;
    }
    if (action < TCOOFF || action > TCION) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int tcflush(int fd, int queue_selector) {
    if (!compat_fd_is_valid(fd)) {
        errno = EBADF;
        return -1;
    }
    if (queue_selector != TCIFLUSH &&
        queue_selector != TCOFLUSH &&
        queue_selector != TCIOFLUSH) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int tcsendbreak(int fd, int duration) {
    if (!compat_fd_is_valid(fd)) {
        errno = EBADF;
        return -1;
    }
    (void)duration;
    return 0;
}

int ioctl(int fd, unsigned long request, ...) {
    va_list ap;
    void *arg;
    struct compat_fd_entry *entry;

    entry = compat_fd_get(fd);
    if (entry == 0) {
        errno = EBADF;
        return -1;
    }

    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);

    switch (request) {
    case FIOCLEX:
        entry->fd_flags |= FD_CLOEXEC;
        return 0;
    case FIONCLEX:
        entry->fd_flags &= ~FD_CLOEXEC;
        return 0;
    case FIONBIO:
        if (arg == 0) {
            errno = EFAULT;
            return -1;
        }
        if (*(const int *)arg) {
            entry->flags |= O_NONBLOCK;
        } else {
            entry->flags &= ~O_NONBLOCK;
        }
        return 0;
    case FIOASYNC:
        if (arg == 0) {
            errno = EFAULT;
            return -1;
        }
        if (*(const int *)arg) {
            entry->flags |= O_ASYNC;
        } else {
            entry->flags &= ~O_ASYNC;
        }
        return 0;
    case FIOSETOWN:
    case TIOCSPGRP:
        return 0;
    case FIONREAD:
        if (arg == 0) {
            errno = EFAULT;
            return -1;
        }
        *(int *)arg = 0;
        return 0;
    case FIOGETOWN:
    case TIOCGPGRP:
        if (arg == 0) {
            errno = EFAULT;
            return -1;
        }
        *(int *)arg = (int)getpid();
        return 0;
    case TIOCGETA:
        if (arg == 0) {
            errno = EFAULT;
            return -1;
        }
        return tcgetattr(fd, (struct termios *)arg);
    case TIOCSETA:
    case TIOCSETAW:
    case TIOCSETAF:
        if (arg == 0) {
            errno = EFAULT;
            return -1;
        }
        return tcsetattr(fd, TCSANOW, (const struct termios *)arg);
    case TIOCGWINSZ:
        if (arg == 0) {
            errno = EFAULT;
            return -1;
        }
        compat_refresh_winsize(&entry->winsize_state);
        *(struct winsize *)arg = entry->winsize_state;
        return 0;
    case TIOCSWINSZ:
        if (arg == 0) {
            errno = EFAULT;
            return -1;
        }
        entry->winsize_state = *(const struct winsize *)arg;
        return 0;
    case SIOCGIFCONF:
    case SIOCGIFFLAGS:
    case SIOCGIFADDR:
    case SIOCGIFNETMASK:
    case SIOCGIFBRDADDR:
    case SIOCGIFDSTADDR:
    case SIOCSIFFLAGS:
        {
            struct mk_network_status status;
            int is_loopback = 0;
            struct ifreq *ifr = (struct ifreq *)arg;

            if (entry->kind != COMPAT_FD_KIND_SOCKET) {
                errno = ENOTSOCK;
                return -1;
            }
            if (arg == 0) {
                errno = EFAULT;
                return -1;
            }
            if (compat_network_status(&status) != 0) {
                errno = ENOSYS;
                return -1;
            }
            if (request == SIOCGIFCONF) {
                return compat_network_fill_ifconf((struct ifconf *)arg, &status);
            }
            if (compat_network_match_interface(ifr->ifr_name, &status, &is_loopback) != 0) {
                return -1;
            }
            if (request == SIOCGIFFLAGS) {
                ifr->ifr_flags = compat_network_interface_flags(&status, is_loopback);
                return 0;
            }
            if (request == SIOCSIFFLAGS) {
                return 0;
            }
            return compat_network_fill_addr(ifr, &status, request, is_loopback);
        }
    default:
        errno = ENOTTY;
        return -1;
    }
}
