#include "compat_fd_state.h"

#include <compat/posix/fcntl.h>
#include <compat/posix/errno.h>
#include <compat/posix/unistd.h>
#include <compat/libc/string.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

static struct compat_fd_entry g_compat_fds[COMPAT_MAX_FDS];
static int g_compat_fd_bootstrapped = 0;

static int compat_fd_is_supported_lock_type(short type) {
    return type == F_RDLCK || type == F_WRLCK || type == F_UNLCK;
}

static void compat_fd_default_winsize(struct winsize *ws) {
    if (ws == 0) {
        return;
    }
    ws->ws_row = 25;
    ws->ws_col = 80;
    ws->ws_xpixel = 0;
    ws->ws_ypixel = 0;
}

static void compat_fd_init_terminal(struct compat_fd_entry *entry) {
    if (entry == 0) {
        return;
    }
    memset(&entry->termios_state, 0, sizeof(entry->termios_state));
    entry->termios_state.c_iflag = BRKINT | ICRNL | IXON;
    entry->termios_state.c_oflag = OPOST | ONLCR;
    entry->termios_state.c_cflag = CREAD | CS8;
    entry->termios_state.c_lflag = ECHO | ECHOE | ECHOK | ICANON | ISIG | IEXTEN;
    memset(entry->termios_state.c_cc, _POSIX_VDISABLE, sizeof(entry->termios_state.c_cc));
    entry->termios_state.c_cc[VINTR] = 3;
    entry->termios_state.c_cc[VQUIT] = 28;
    entry->termios_state.c_cc[VERASE] = 127;
    entry->termios_state.c_cc[VKILL] = 21;
    entry->termios_state.c_cc[VEOF] = 4;
    entry->termios_state.c_cc[VSTART] = 17;
    entry->termios_state.c_cc[VSTOP] = 19;
    entry->termios_state.c_cc[VSUSP] = 26;
    entry->termios_state.c_cc[VMIN] = 1;
    entry->termios_state.c_cc[VTIME] = 0;
    entry->termios_state.c_ispeed = (int)B38400;
    entry->termios_state.c_ospeed = (int)B38400;
    compat_fd_default_winsize(&entry->winsize_state);
}

static void compat_fd_init_socket(struct compat_fd_entry *entry, int domain, int type, int protocol) {
    if (entry == 0) {
        return;
    }
    entry->socket_domain = domain;
    entry->socket_type = type;
    entry->socket_protocol = protocol;
    entry->socket_state_flags = 0;
    entry->socket_ready_flags = COMPAT_SOCKET_READY_SEND;
    entry->socket_options = 0;
    entry->socket_error = 0;
    entry->socket_sndbuf = 4096;
    entry->socket_rcvbuf = 4096;
}

void compat_fd_bootstrap(void) {
    struct compat_fd_entry *entry;

    if (g_compat_fd_bootstrapped) {
        return;
    }

    memset(g_compat_fds, 0, sizeof(g_compat_fds));

    entry = &g_compat_fds[STDIN_FILENO];
    entry->used = 1;
    entry->kind = COMPAT_FD_KIND_FILE;
    entry->host_fd = -1;
    entry->flags = O_RDONLY;
    entry->is_tty = 1;
    entry->is_stdio = 1;
    compat_fd_init_terminal(entry);

    entry = &g_compat_fds[STDOUT_FILENO];
    entry->used = 1;
    entry->kind = COMPAT_FD_KIND_FILE;
    entry->host_fd = -1;
    entry->flags = O_WRONLY;
    entry->is_tty = 1;
    entry->is_stdio = 1;
    compat_fd_init_terminal(entry);

    entry = &g_compat_fds[STDERR_FILENO];
    entry->used = 1;
    entry->kind = COMPAT_FD_KIND_FILE;
    entry->host_fd = -1;
    entry->flags = O_WRONLY;
    entry->is_tty = 1;
    entry->is_stdio = 1;
    compat_fd_init_terminal(entry);

    g_compat_fd_bootstrapped = 1;
}

int compat_fd_allocate(int min_fd) {
    int fd;

    compat_fd_bootstrap();

    if (min_fd < 0) {
        min_fd = 0;
    }
    if (min_fd < 3) {
        min_fd = 3;
    }

    for (fd = min_fd; fd < COMPAT_MAX_FDS; ++fd) {
        if (!g_compat_fds[fd].used) {
            return fd;
        }
    }
    return -1;
}

struct compat_fd_entry *compat_fd_get(int fd) {
    compat_fd_bootstrap();
    if (fd < 0 || fd >= COMPAT_MAX_FDS || !g_compat_fds[fd].used) {
        return 0;
    }
    return &g_compat_fds[fd];
}

const struct compat_fd_entry *compat_fd_get_const(int fd) {
    return compat_fd_get(fd);
}

int compat_fd_bind(int fd, int host_fd, int flags, int is_tty, int is_stdio) {
    struct compat_fd_entry *entry;

    compat_fd_bootstrap();
    if (fd < 0 || fd >= COMPAT_MAX_FDS) {
        return -1;
    }

    entry = &g_compat_fds[fd];
    memset(entry, 0, sizeof(*entry));
    entry->used = 1;
    entry->kind = COMPAT_FD_KIND_FILE;
    entry->host_fd = host_fd;
    entry->flags = flags;
    entry->fd_flags = 0;
    entry->is_tty = is_tty != 0;
    entry->is_stdio = is_stdio != 0;
    entry->lock_type = F_UNLCK;
    entry->lock_start = 0;
    entry->lock_len = 0;
    entry->lock_pid = 0;

    if (entry->is_tty) {
        compat_fd_init_terminal(entry);
    }

    return 0;
}

int compat_fd_bind_socket(int fd, int host_fd, int domain, int type, int protocol) {
    struct compat_fd_entry *entry;

    compat_fd_bootstrap();
    if (fd < 0 || fd >= COMPAT_MAX_FDS) {
        return -1;
    }

    entry = &g_compat_fds[fd];
    memset(entry, 0, sizeof(*entry));
    entry->used = 1;
    entry->kind = COMPAT_FD_KIND_SOCKET;
    entry->host_fd = host_fd;
    entry->flags = O_RDWR;
    entry->fd_flags = 0;
    entry->lock_type = F_UNLCK;
    entry->lock_start = 0;
    entry->lock_len = 0;
    entry->lock_pid = 0;
    compat_fd_init_socket(entry, domain, type, protocol);
    return 0;
}

int compat_fd_duplicate(int src_fd, int dst_fd, int cloexec) {
    const struct compat_fd_entry *src;

    compat_fd_bootstrap();
    src = compat_fd_get_const(src_fd);
    if (src == 0 || dst_fd < 0 || dst_fd >= COMPAT_MAX_FDS) {
        return -1;
    }

    g_compat_fds[dst_fd] = *src;
    g_compat_fds[dst_fd].used = 1;
    if (cloexec) {
        g_compat_fds[dst_fd].fd_flags |= FD_CLOEXEC;
    } else {
        g_compat_fds[dst_fd].fd_flags &= ~FD_CLOEXEC;
    }
    return 0;
}

int compat_fd_refs_host(int host_fd, int kind, int skip_fd) {
    int fd;

    compat_fd_bootstrap();
    for (fd = 0; fd < COMPAT_MAX_FDS; ++fd) {
        if (fd == skip_fd) {
            continue;
        }
        if (g_compat_fds[fd].used &&
            g_compat_fds[fd].host_fd == host_fd &&
            g_compat_fds[fd].kind == kind) {
            return 1;
        }
    }
    return 0;
}

void compat_fd_reset(int fd) {
    if (fd < 0 || fd >= COMPAT_MAX_FDS) {
        return;
    }

    memset(&g_compat_fds[fd], 0, sizeof(g_compat_fds[fd]));
    g_compat_fds[fd].host_fd = -1;
}

int compat_fd_is_valid(int fd) {
    return compat_fd_get_const(fd) != 0;
}

int compat_fd_is_tty(int fd) {
    const struct compat_fd_entry *entry = compat_fd_get_const(fd);

    return entry != 0 && entry->is_tty;
}

int compat_fd_is_socket(int fd) {
    const struct compat_fd_entry *entry = compat_fd_get_const(fd);

    return entry != 0 && entry->kind == COMPAT_FD_KIND_SOCKET;
}

int compat_fd_socket_readiness(int fd) {
    const struct compat_fd_entry *entry = compat_fd_get_const(fd);

    if (entry == 0 || entry->kind != COMPAT_FD_KIND_SOCKET) {
        return 0;
    }
    return entry->socket_ready_flags;
}

void compat_fd_socket_update_ready_by_host(int host_fd, int set_mask, int clear_mask) {
    int fd;

    compat_fd_bootstrap();
    for (fd = 0; fd < COMPAT_MAX_FDS; ++fd) {
        struct compat_fd_entry *entry = &g_compat_fds[fd];

        if (!entry->used ||
            entry->kind != COMPAT_FD_KIND_SOCKET ||
            entry->host_fd != host_fd) {
            continue;
        }
        entry->socket_ready_flags |= set_mask;
        entry->socket_ready_flags &= ~clear_mask;
    }
}

void compat_fd_socket_set_error_by_host(int host_fd, int error) {
    int fd;

    compat_fd_bootstrap();
    for (fd = 0; fd < COMPAT_MAX_FDS; ++fd) {
        struct compat_fd_entry *entry = &g_compat_fds[fd];

        if (!entry->used ||
            entry->kind != COMPAT_FD_KIND_SOCKET ||
            entry->host_fd != host_fd) {
            continue;
        }
        entry->socket_error = error;
    }
}

int compat_fd_get_lock(int fd, struct flock *lock) {
    const struct compat_fd_entry *entry;

    if (lock == 0) {
        errno = EINVAL;
        return -1;
    }

    entry = compat_fd_get_const(fd);
    if (entry == 0) {
        errno = EBADF;
        return -1;
    }

    if (!compat_fd_is_supported_lock_type(lock->l_type)) {
        errno = EINVAL;
        return -1;
    }

    if (entry->lock_type == F_UNLCK) {
        lock->l_type = F_UNLCK;
        lock->l_pid = 0;
        return 0;
    }

    lock->l_type = (short)entry->lock_type;
    lock->l_whence = SEEK_SET;
    lock->l_start = entry->lock_start;
    lock->l_len = entry->lock_len;
    lock->l_pid = entry->lock_pid;
    return 0;
}

int compat_fd_set_lock(int fd, const struct flock *lock, int wait) {
    struct compat_fd_entry *entry;

    (void)wait;

    if (lock == 0) {
        errno = EINVAL;
        return -1;
    }
    if (!compat_fd_is_supported_lock_type(lock->l_type)) {
        errno = EINVAL;
        return -1;
    }
    if (lock->l_whence != SEEK_SET && lock->l_whence != SEEK_CUR && lock->l_whence != SEEK_END) {
        errno = EINVAL;
        return -1;
    }

    entry = compat_fd_get(fd);
    if (entry == 0) {
        errno = EBADF;
        return -1;
    }

    entry->lock_type = lock->l_type;
    entry->lock_start = lock->l_start;
    entry->lock_len = lock->l_len;
    entry->lock_pid = (lock->l_type == F_UNLCK) ? 0 : getpid();
    return 0;
}
