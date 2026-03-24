#ifndef VIBE_BSDGAME_TIME_H
#define VIBE_BSDGAME_TIME_H

#include <compat_defs.h>

typedef int clockid_t;

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
    long tm_gmtoff;
    const char *tm_zone;
};

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1

#define timespecclear(ts) \
    do { \
        (ts)->tv_sec = 0; \
        (ts)->tv_nsec = 0; \
    } while (0)

#define timespecisset(ts) ((ts)->tv_sec != 0 || (ts)->tv_nsec != 0)

#define timespeccmp(lhs, rhs, op) \
    (((lhs)->tv_sec == (rhs)->tv_sec) ? \
        ((lhs)->tv_nsec op (rhs)->tv_nsec) : \
        ((lhs)->tv_sec op (rhs)->tv_sec))

#define timespecadd(a, b, result) \
    do { \
        (result)->tv_sec = (a)->tv_sec + (b)->tv_sec; \
        (result)->tv_nsec = (a)->tv_nsec + (b)->tv_nsec; \
        while ((result)->tv_nsec >= 1000000000L) { \
            (result)->tv_nsec -= 1000000000L; \
            (result)->tv_sec += 1; \
        } \
    } while (0)

#define timespecsub(a, b, result) \
    do { \
        (result)->tv_sec = (a)->tv_sec - (b)->tv_sec; \
        (result)->tv_nsec = (a)->tv_nsec - (b)->tv_nsec; \
        while ((result)->tv_nsec < 0) { \
            (result)->tv_nsec += 1000000000L; \
            (result)->tv_sec -= 1; \
        } \
    } while (0)

time_t time(time_t *out);
int clock_gettime(clockid_t clock_id, struct timespec *ts);
int nanosleep(const struct timespec *req, struct timespec *rem);
struct tm *gmtime(const time_t *timep);
struct tm *localtime(const time_t *timep);
time_t mktime(struct tm *tm);
char *ctime(const time_t *timep);
size_t strftime(char *dst, size_t size, const char *format,
                const struct tm *tm);

#endif
