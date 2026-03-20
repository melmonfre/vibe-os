#include <time.h>
#include <lang/include/vibe_app_runtime.h>
#include <compat/posix/errno.h>

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

#ifndef TIME_UTC
#define TIME_UTC 1
#endif

static void ticks_to_timespec(unsigned int ticks, struct timespec *ts) {
    unsigned int hz = vibe_app_clock_hz();
    if (!ts || hz == 0u) {
        return;
    }
    ts->tv_sec = (time_t)(ticks / hz);
    ts->tv_nsec = (long)(((unsigned long long)(ticks % hz) * 1000000000ull) / (unsigned long long)hz);
}

time_t time(time_t *out) {
    time_t now = (time_t)(vibe_app_ticks() / vibe_app_clock_hz());
    if (out) {
        *out = now;
    }
    return now;
}

clock_t clock(void) {
    return (clock_t)vibe_app_ticks();
}

int clock_getres(clockid_t id, struct timespec *ts) {
    (void)id;
    if (!ts) {
        errno = EINVAL;
        return -1;
    }
    ts->tv_sec = 0;
    ts->tv_nsec = 1000000000l / (long)vibe_app_clock_hz();
    return 0;
}

int clock_gettime(clockid_t id, struct timespec *ts) {
    (void)id;
    if (!ts) {
        errno = EINVAL;
        return -1;
    }
    ticks_to_timespec(vibe_app_ticks(), ts);
    return 0;
}

int clock_settime(clockid_t id, const struct timespec *ts) {
    (void)id;
    (void)ts;
    errno = EINVAL;
    return -1;
}

int gettimeofday(struct timeval *tv, void *tz) {
    unsigned int ticks;
    unsigned int hz;
    unsigned long long micros;

    (void)tz;
    if (!tv) {
        errno = EINVAL;
        return -1;
    }
    ticks = vibe_app_ticks();
    hz = vibe_app_clock_hz();
    if (hz == 0u) {
        errno = EINVAL;
        return -1;
    }
    micros = ((unsigned long long)ticks * 1000000ull) / (unsigned long long)hz;
    tv->tv_sec = (time_t)(micros / 1000000ull);
    tv->tv_usec = (long)(micros % 1000000ull);
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    unsigned long long total_ms;

    if (!req || req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000l) {
        errno = EINVAL;
        return -1;
    }
    total_ms = (unsigned long long)req->tv_sec * 1000ull;
    total_ms += (unsigned long long)(req->tv_nsec / 1000000l);
    if (req->tv_nsec > 0 && (req->tv_nsec % 1000000l) != 0) {
        total_ms += 1ull;
    }
    if (total_ms > 0xffffffffull) {
        total_ms = 0xffffffffull;
    }
    (void)vibe_app_sleep_ms((unsigned int)total_ms);
    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    return 0;
}

int timespec_get(struct timespec *ts, int base) {
    if (!ts || base != TIME_UTC) {
        return 0;
    }
    if (clock_gettime(CLOCK_REALTIME, ts) != 0) {
        return 0;
    }
    return base;
}
