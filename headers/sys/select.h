#ifndef VIBE_SYS_SELECT_H
#define VIBE_SYS_SELECT_H

#include <sys/types.h>
#include <errno.h>

#ifndef FD_SETSIZE
#define FD_SETSIZE 1024
#endif

#ifndef _TIMEVAL_DECLARED
#define _TIMEVAL_DECLARED
struct timeval {
    time_t tv_sec;
    suseconds_t tv_usec;
};
#endif

#ifndef _TIMESPEC_DECLARED
#define _TIMESPEC_DECLARED
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};
#endif

typedef unsigned int __fd_mask;
#define __NBBY 8U
#define __NFDBITS ((unsigned int)(sizeof(__fd_mask) * __NBBY))
#define __howmany(x, y) (((x) + ((y) - 1)) / (y))

typedef struct fd_set {
    __fd_mask fds_bits[__howmany(FD_SETSIZE, __NFDBITS)];
} fd_set;

static inline void __fd_set_inline(int fd, fd_set *set) {
    if (set != 0 && fd >= 0 && fd < FD_SETSIZE) {
        set->fds_bits[(unsigned int)fd / __NFDBITS] |=
            (__fd_mask)(1U << ((unsigned int)fd % __NFDBITS));
    }
}

static inline void __fd_clr_inline(int fd, fd_set *set) {
    if (set != 0 && fd >= 0 && fd < FD_SETSIZE) {
        set->fds_bits[(unsigned int)fd / __NFDBITS] &=
            (__fd_mask)~(1U << ((unsigned int)fd % __NFDBITS));
    }
}

static inline int __fd_isset_inline(int fd, const fd_set *set) {
    if (set == 0 || fd < 0 || fd >= FD_SETSIZE) {
        return 0;
    }
    return (set->fds_bits[(unsigned int)fd / __NFDBITS] &
            (__fd_mask)(1U << ((unsigned int)fd % __NFDBITS))) != 0;
}

#define FD_SET(n, p) __fd_set_inline((n), (p))
#define FD_CLR(n, p) __fd_clr_inline((n), (p))
#define FD_ISSET(n, p) __fd_isset_inline((n), (p))
#define FD_ZERO(p) do { \
    unsigned int __i; \
    fd_set *__set = (p); \
    for (__i = 0; __set != 0 && __i < __howmany(FD_SETSIZE, __NFDBITS); ++__i) { \
        __set->fds_bits[__i] = 0; \
    } \
} while (0)
#define FD_COPY(f, t) do { \
    if ((f) != 0 && (t) != 0) { \
        *(t) = *(f); \
    } \
} while (0)

typedef unsigned int sigset_t;

static inline int select(int nfds, fd_set *readfds, fd_set *writefds,
                         fd_set *exceptfds, struct timeval *timeout) {
    (void)nfds;
    if (readfds != 0) {
        FD_ZERO(readfds);
    }
    if (writefds != 0) {
        FD_ZERO(writefds);
    }
    if (exceptfds != 0) {
        FD_ZERO(exceptfds);
    }
    if (timeout != 0 && timeout->tv_sec == 0 && timeout->tv_usec == 0) {
        return 0;
    }
    errno = ENOSYS;
    return -1;
}

static inline int pselect(int nfds, fd_set *readfds, fd_set *writefds,
                          fd_set *exceptfds, const struct timespec *timeout,
                          const sigset_t *sigmask) {
    (void)nfds;
    (void)readfds;
    (void)writefds;
    (void)exceptfds;
    (void)timeout;
    (void)sigmask;
    errno = ENOSYS;
    return -1;
}

#endif
