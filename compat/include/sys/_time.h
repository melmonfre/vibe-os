#ifndef _COMPAT_SYS__TIME_H_
#define _COMPAT_SYS__TIME_H_

#include <compat_defs.h>

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

struct timeval {
    time_t tv_sec;
    long tv_usec;
};

#endif
