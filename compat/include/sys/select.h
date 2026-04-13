#ifndef COMPAT_SYS_SELECT_H
#define COMPAT_SYS_SELECT_H

#include <compat/posix/errno.h>
#include <sys/types.h>

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

int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout);
int pselect(int nfds, fd_set *readfds, fd_set *writefds,
            fd_set *exceptfds, const struct timespec *timeout,
            const sigset_t *sigmask);

#endif
