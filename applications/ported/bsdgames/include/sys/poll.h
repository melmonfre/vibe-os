#ifndef VIBE_BSDGAME_SYS_POLL_H
#define VIBE_BSDGAME_SYS_POLL_H

#include <signal.h>
#include <time.h>

typedef unsigned long nfds_t;

struct pollfd {
    int fd;
    short events;
    short revents;
};

#define POLLIN 0x0001
#define POLLPRI 0x0002
#define POLLOUT 0x0004
#define POLLERR 0x0008
#define POLLHUP 0x0010
#define POLLNVAL 0x0020

int poll(struct pollfd *fds, nfds_t nfds, int timeout);
int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout_ts,
          const sigset_t *sigmask);

#endif
