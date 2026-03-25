#ifndef _COMPAT_SYS_TIME_H_
#define _COMPAT_SYS_TIME_H_

#include <time.h>
#include <sys/_time.h>

int gettimeofday(struct timeval *tv, void *tz);

#endif
