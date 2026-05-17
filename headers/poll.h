#ifndef VIBE_POLL_H
#define VIBE_POLL_H

#include <sys/select.h>
#include <errno.h>

typedef struct pollfd {
    int fd;
    short events;
    short revents;
} pollfd_t;

typedef unsigned int nfds_t;

#define POLLIN    0x0001
#define POLLPRI   0x0002
#define POLLOUT   0x0004
#define POLLERR   0x0008
#define POLLHUP   0x0010
#define POLLNVAL  0x0020
#define POLLRDNORM 0x0040
#define POLLNORM POLLRDNORM
#define POLLWRNORM POLLOUT
#define POLLRDBAND 0x0080
#define POLLWRBAND 0x0100

#define INFTIM (-1)

static inline int poll(struct pollfd fds[], nfds_t nfds, int timeout) {
    nfds_t i;

    for (i = 0; i < nfds; ++i) {
        fds[i].revents = 0;
        if (fds[i].fd < 0) {
            fds[i].revents = POLLNVAL;
        }
    }
    if (timeout == 0) {
        return 0;
    }
    errno = ENOSYS;
    return -1;
}

static inline int ppoll(struct pollfd fds[], nfds_t nfds,
                        const struct timespec *timeout,
                        const sigset_t *sigmask) {
    (void)fds;
    (void)nfds;
    (void)timeout;
    (void)sigmask;
    errno = ENOSYS;
    return -1;
}

#endif
