#ifndef VIBE_BSDGAME_SYS_TIME_H
#define VIBE_BSDGAME_SYS_TIME_H

#include <compat_defs.h>

struct timeval {
    long tv_sec;
    long tv_usec;
};

struct itimerval {
    struct timeval it_interval;
    struct timeval it_value;
};

#define ITIMER_REAL 0

int setitimer(int which, const struct itimerval *new_value,
              struct itimerval *old_value);

#endif
