#ifndef COMPAT_POSIX_FD_STATE_H
#define COMPAT_POSIX_FD_STATE_H

#include <compat/posix/fcntl.h>
#include <sys/ioctl.h>
#include <sys/termios.h>

#define COMPAT_MAX_FDS 32

#define COMPAT_SOCKET_READY_RECV    0x01
#define COMPAT_SOCKET_READY_SEND    0x02
#define COMPAT_SOCKET_READY_ACCEPT  0x04
#define COMPAT_SOCKET_READY_CONNECT 0x08

enum compat_fd_kind {
    COMPAT_FD_KIND_NONE = 0,
    COMPAT_FD_KIND_FILE = 1,
    COMPAT_FD_KIND_SOCKET = 2
};

struct compat_fd_entry {
    int used;
    int kind;
    int host_fd;
    int flags;
    int fd_flags;
    int is_tty;
    int is_stdio;
    int lock_type;
    off_t lock_start;
    off_t lock_len;
    pid_t lock_pid;
    int socket_domain;
    int socket_type;
    int socket_protocol;
    int socket_state_flags;
    int socket_ready_flags;
    int socket_options;
    int socket_error;
    int socket_sndbuf;
    int socket_rcvbuf;
    struct termios termios_state;
    struct winsize winsize_state;
};

void compat_fd_bootstrap(void);
int compat_fd_allocate(int min_fd);
struct compat_fd_entry *compat_fd_get(int fd);
const struct compat_fd_entry *compat_fd_get_const(int fd);
int compat_fd_bind(int fd, int host_fd, int flags, int is_tty, int is_stdio);
int compat_fd_bind_socket(int fd, int host_fd, int domain, int type, int protocol);
int compat_fd_duplicate(int src_fd, int dst_fd, int cloexec);
int compat_fd_refs_host(int host_fd, int kind, int skip_fd);
void compat_fd_reset(int fd);
int compat_fd_is_valid(int fd);
int compat_fd_is_tty(int fd);
int compat_fd_is_socket(int fd);
int compat_fd_socket_readiness(int fd);
void compat_fd_socket_update_ready_by_host(int host_fd, int set_mask, int clear_mask);
void compat_fd_socket_set_error_by_host(int host_fd, int error);
int compat_fd_get_lock(int fd, struct flock *lock);
int compat_fd_set_lock(int fd, const struct flock *lock, int wait);

#endif
