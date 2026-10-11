#ifndef _SYS_TIME_H_
#define _SYS_TIME_H_

#include <sys/cdefs.h>
#include <sys/_time.h>
#include <time.h>

struct itimerval {
    struct timeval it_interval;
    struct timeval it_value;
};

#define ITIMER_REAL     0
#define ITIMER_VIRTUAL  1
#define ITIMER_PROF     2

#define timerclear(tvp)     ((tvp)->tv_sec = (tvp)->tv_usec = 0)
#define timerisset(tvp)     ((tvp)->tv_sec != 0 || (tvp)->tv_usec != 0)
#define timercmp(tvp, uvp, cmp) \
    ((tvp)->tv_sec cmp (uvp)->tv_sec || \
     ((tvp)->tv_sec == (uvp)->tv_sec && (tvp)->tv_usec cmp (uvp)->tv_usec))
#define timeradd(tvp, uvp, vvp) \
    do { \
        (vvp)->tv_sec = (tvp)->tv_sec + (uvp)->tv_sec; \
        (vvp)->tv_usec = (tvp)->tv_usec + (uvp)->tv_usec; \
        if ((vvp)->tv_usec >= 1000000) { \
            (vvp)->tv_sec++; \
            (vvp)->tv_usec -= 1000000; \
        } \
    } while (0)
#define timersub(tvp, uvp, vvp) \
    do { \
        (vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec; \
        (vvp)->tv_usec = (tvp)->tv_usec - (uvp)->tv_usec; \
        if ((vvp)->tv_usec < 0) { \
            (vvp)->tv_sec--; \
            (vvp)->tv_usec += 1000000; \
        } \
    } while (0)

#define TIMEVAL_TO_TIMESPEC(tv, ts) \
    do { \
        (ts)->tv_sec = (tv)->tv_sec; \
        (ts)->tv_nsec = (tv)->tv_usec * 1000; \
    } while (0)
#define TIMESPEC_TO_TIMEVAL(tv, ts) \
    do { \
        (tv)->tv_sec = (ts)->tv_sec; \
        (tv)->tv_usec = (ts)->tv_nsec / 1000; \
    } while (0)
#define timespecsub(tsp, usp, vsp) \
    do { \
        (vsp)->tv_sec = (tsp)->tv_sec - (usp)->tv_sec; \
        (vsp)->tv_nsec = (tsp)->tv_nsec - (usp)->tv_nsec; \
        if ((vsp)->tv_nsec < 0) { \
            (vsp)->tv_sec--; \
            (vsp)->tv_nsec += 1000000000L; \
        } \
    } while (0)

int gettimeofday(struct timeval *tv, void *tz);
int settimeofday(const struct timeval *tv, const void *tz);
int getitimer(int which, struct itimerval *value);
int setitimer(int which, const struct itimerval *restrict value, struct itimerval *restrict ovalue);
int utimes(const char *path, const struct timeval times[2]);

#endif
