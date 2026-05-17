#ifndef VIBE_SYS_TIME_H
#define VIBE_SYS_TIME_H

#include <sys/select.h>

#define TIMEVAL_TO_TIMESPEC(tv, ts) do { \
    (ts)->tv_sec = (tv)->tv_sec; \
    (ts)->tv_nsec = (long)((tv)->tv_usec * 1000); \
} while (0)

#define TIMESPEC_TO_TIMEVAL(tv, ts) do { \
    (tv)->tv_sec = (ts)->tv_sec; \
    (tv)->tv_usec = (suseconds_t)((ts)->tv_nsec / 1000L); \
} while (0)

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

#define DST_NONE 0
#define DST_USA  1
#define DST_AUST 2
#define DST_WET  3
#define DST_MET  4
#define DST_EET  5
#define DST_CAN  6

#define timerclear(tvp) ((tvp)->tv_sec = 0, (tvp)->tv_usec = 0)
#define timerisset(tvp) ((tvp)->tv_sec != 0 || (tvp)->tv_usec != 0)
#define timerisvalid(tvp) ((tvp)->tv_usec >= 0 && (tvp)->tv_usec < 1000000)
#define timercmp(tvp, uvp, cmp) \
    (((tvp)->tv_sec == (uvp)->tv_sec) ? \
        ((tvp)->tv_usec cmp (uvp)->tv_usec) : \
        ((tvp)->tv_sec cmp (uvp)->tv_sec))
#define timeradd(tvp, uvp, vvp) do { \
    (vvp)->tv_sec = (tvp)->tv_sec + (uvp)->tv_sec; \
    (vvp)->tv_usec = (tvp)->tv_usec + (uvp)->tv_usec; \
    if ((vvp)->tv_usec >= 1000000) { \
        (vvp)->tv_sec++; \
        (vvp)->tv_usec -= 1000000; \
    } \
} while (0)
#define timersub(tvp, uvp, vvp) do { \
    (vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec; \
    (vvp)->tv_usec = (tvp)->tv_usec - (uvp)->tv_usec; \
    if ((vvp)->tv_usec < 0) { \
        (vvp)->tv_sec--; \
        (vvp)->tv_usec += 1000000; \
    } \
} while (0)

#define timespecclear(tsp) ((tsp)->tv_sec = 0, (tsp)->tv_nsec = 0)
#define timespecisset(tsp) ((tsp)->tv_sec != 0 || (tsp)->tv_nsec != 0)
#define timespecisvalid(tsp) ((tsp)->tv_nsec >= 0 && (tsp)->tv_nsec < 1000000000L)
#define timespeccmp(tsp, usp, cmp) \
    (((tsp)->tv_sec == (usp)->tv_sec) ? \
        ((tsp)->tv_nsec cmp (usp)->tv_nsec) : \
        ((tsp)->tv_sec cmp (usp)->tv_sec))
#define timespecadd(tsp, usp, vsp) do { \
    (vsp)->tv_sec = (tsp)->tv_sec + (usp)->tv_sec; \
    (vsp)->tv_nsec = (tsp)->tv_nsec + (usp)->tv_nsec; \
    if ((vsp)->tv_nsec >= 1000000000L) { \
        (vsp)->tv_sec++; \
        (vsp)->tv_nsec -= 1000000000L; \
    } \
} while (0)
#define timespecsub(tsp, usp, vsp) do { \
    (vsp)->tv_sec = (tsp)->tv_sec - (usp)->tv_sec; \
    (vsp)->tv_nsec = (tsp)->tv_nsec - (usp)->tv_nsec; \
    if ((vsp)->tv_nsec < 0) { \
        (vsp)->tv_sec--; \
        (vsp)->tv_nsec += 1000000000L; \
    } \
} while (0)

#define ITIMER_REAL 0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF 2

struct itimerval {
    struct timeval it_interval;
    struct timeval it_value;
};

struct clockinfo {
    int hz;
    int tick;
    int stathz;
    int profhz;
};

int gettimeofday(struct timeval *tv, struct timezone *tz);
int settimeofday(const struct timeval *tv, const struct timezone *tz);

#endif
