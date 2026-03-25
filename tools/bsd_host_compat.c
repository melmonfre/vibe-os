#include "bsd_host_compat.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *g_progname = "bsd-host-tool";
static uint32_t g_random_state = 0u;

int pledge(const char *promises, const char *execpromises) {
    (void)promises;
    (void)execpromises;
    return 0;
}

int unveil(const char *path, const char *permissions) {
    (void)path;
    (void)permissions;
    return 0;
}

void setprogname(const char *progname) {
    if (progname && progname[0] != '\0') {
        const char *base = strrchr(progname, '/');

        g_progname = base ? base + 1 : progname;
    }
}

const char *getprogname(void) {
    return g_progname;
}

size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t src_len = src ? strlen(src) : 0u;

    if (size != 0u) {
        size_t copy_len = src_len;

        if (copy_len >= size) {
            copy_len = size - 1u;
        }
        if (copy_len != 0u && src) {
            memcpy(dst, src, copy_len);
        }
        dst[copy_len] = '\0';
    }
    return src_len;
}

size_t strlcat(char *dst, const char *src, size_t size) {
    size_t dst_len = dst ? strlen(dst) : 0u;
    size_t src_len = src ? strlen(src) : 0u;

    if (dst_len >= size) {
        return size + src_len;
    }
    return dst_len + strlcpy(dst + dst_len, src, size - dst_len);
}

long long strtonum(const char *text, long long minval, long long maxval,
                   const char **errstrp) {
    char *end = NULL;
    long long value;

    if (errstrp) {
        *errstrp = NULL;
    }
    if (!text || text[0] == '\0') {
        if (errstrp) {
            *errstrp = "invalid";
        }
        return 0;
    }

    value = strtoll(text, &end, 10);
    if (!end || *end != '\0') {
        if (errstrp) {
            *errstrp = "invalid";
        }
        return 0;
    }
    if (value < minval) {
        if (errstrp) {
            *errstrp = "too small";
        }
        return 0;
    }
    if (value > maxval) {
        if (errstrp) {
            *errstrp = "too large";
        }
        return 0;
    }
    return value;
}

static uint32_t host_random_next(void) {
    if (g_random_state == 0u) {
        g_random_state = (uint32_t)time(NULL);
        if (g_random_state == 0u) {
            g_random_state = 0x12345678u;
        }
    }
    g_random_state = g_random_state * 1664525u + 1013904223u;
    return g_random_state;
}

uint32_t arc4random(void) {
    return host_random_next();
}

uint32_t arc4random_uniform(uint32_t upper_bound) {
    uint32_t threshold;
    uint32_t value;

    if (upper_bound == 0u) {
        return 0u;
    }
    threshold = (uint32_t)(0u - upper_bound) % upper_bound;
    do {
        value = arc4random();
    } while (value < threshold);
    return value % upper_bound;
}

void *reallocarray(void *ptr, size_t nmemb, size_t size) {
    if (nmemb != 0u && size > SIZE_MAX / nmemb) {
        return NULL;
    }
    return realloc(ptr, nmemb * size);
}
