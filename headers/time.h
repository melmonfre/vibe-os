#ifndef VIBE_TIME_H
#define VIBE_TIME_H

#include <sys/time.h>
#include <sys/types.h>

#define CLOCKS_PER_SEC 100

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 4
#define TIME_UTC CLOCK_REALTIME

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
};

clock_t clock(void);
time_t time(time_t *timer);
double difftime(time_t time1, time_t time0);
time_t mktime(struct tm *tm);
struct tm *gmtime(const time_t *timer);
struct tm *localtime(const time_t *timer);
struct tm *gmtime_r(const time_t *timer, struct tm *result);
struct tm *localtime_r(const time_t *timer, struct tm *result);
char *asctime(const struct tm *tm);
char *ctime(const time_t *timer);
size_t strftime(char *dst, size_t size, const char *format, const struct tm *tm);
int nanosleep(const struct timespec *req, struct timespec *rem);
int clock_gettime(clockid_t clock_id, struct timespec *tp);

#endif
