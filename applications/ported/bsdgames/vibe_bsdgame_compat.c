#include "vibe_bsdgame_shim.h"

#include <lang/include/vibe_app_runtime.h>

char *__progname = "bsdgame";

static char g_progname_storage[64] = "bsdgame";
static uint32_t g_bsdgame_random_state = 0u;
static void (*g_signal_handlers[32])(int);
static struct tm g_bsdgame_gmtime;
static struct tm g_bsdgame_localtime;
static const char g_bsdgame_tz_utc[] = "UTC";
static char g_bsdgame_locale_name[] = "C";
static struct lconv g_bsdgame_locale = {
    ".", "", "", "", "", "", "", "", "", "",
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const char *bsdgame_basename(const char *path) {
    const char *last = path;

    if (!path || path[0] == '\0') {
        return "bsdgame";
    }
    while (*path != '\0') {
        if (*path == '/') {
            last = path + 1;
        }
        ++path;
    }
    return (*last != '\0') ? last : "bsdgame";
}

static uint32_t bsdgame_next_random(void) {
    if (g_bsdgame_random_state == 0u) {
        time_t now = time(0);

        g_bsdgame_random_state = (uint32_t)(now != 0 ? now : 0x12345678u);
    }
    g_bsdgame_random_state = (g_bsdgame_random_state * 1664525u) + 1013904223u;
    return g_bsdgame_random_state;
}

static int bsdgame_is_leap_year(int year) {
    return ((year % 4) == 0 && ((year % 100) != 0 || (year % 400) == 0));
}

static int bsdgame_days_in_month(int year, int month) {
    static const int month_lengths[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    if (month == 1 && bsdgame_is_leap_year(year)) {
        return 29;
    }
    if (month < 0 || month >= 12) {
        return 30;
    }
    return month_lengths[month];
}

static void bsdgame_fill_tm_from_days(long long days_since_epoch,
                                      int seconds_of_day,
                                      struct tm *tm) {
    long long remaining_days = days_since_epoch;
    int year = 1970;
    int month = 0;
    int yday = 0;

    while (remaining_days >= (long long)(bsdgame_is_leap_year(year) ? 366 : 365)) {
        remaining_days -= (long long)(bsdgame_is_leap_year(year) ? 366 : 365);
        ++year;
    }
    while (month < 12) {
        int dim = bsdgame_days_in_month(year, month);

        if (remaining_days < dim) {
            break;
        }
        remaining_days -= dim;
        yday += dim;
        ++month;
    }

    tm->tm_year = year - 1900;
    tm->tm_mon = month;
    tm->tm_mday = (int)remaining_days + 1;
    tm->tm_yday = yday + (int)remaining_days;
    tm->tm_hour = seconds_of_day / 3600;
    tm->tm_min = (seconds_of_day / 60) % 60;
    tm->tm_sec = seconds_of_day % 60;
    tm->tm_wday = (int)((days_since_epoch + 4ll) % 7ll);
    if (tm->tm_wday < 0) {
        tm->tm_wday += 7;
    }
    tm->tm_isdst = 0;
    tm->tm_gmtoff = 0;
    tm->tm_zone = g_bsdgame_tz_utc;
}

static void bsdgame_seconds_to_tm(time_t seconds, struct tm *tm) {
    long long total_seconds;
    long long total_days;
    int seconds_of_day;

    if (!tm) {
        return;
    }
    total_seconds = (long long)seconds;
    if (total_seconds < 0) {
        total_seconds = 0;
    }
    total_days = total_seconds / 86400ll;
    seconds_of_day = (int)(total_seconds % 86400ll);
    bsdgame_fill_tm_from_days(total_days, seconds_of_day, tm);
}

static void bsdgame_write_message(FILE *stream,
                                  const char *prefix,
                                  int code,
                                  const char *fmt,
                                  va_list ap,
                                  int include_errno) {
    if (prefix && prefix[0] != '\0') {
        fprintf(stream, "%s: ", prefix);
    }
    if (fmt && fmt[0] != '\0') {
        vfprintf(stream, fmt, ap);
    }
    if (include_errno) {
        fprintf(stream, ": errno %d", code);
    }
    fputc('\n', stream);
}

int isblank(int c) {
    return c == ' ' || c == '\t';
}

int abs(int value) {
    return value < 0 ? -value : value;
}

int atoi(const char *text) {
    return (int)strtol(text, 0, 10);
}

long atol(const char *text) {
    return strtol(text, 0, 10);
}

long long atoll(const char *text) {
    return strtoll(text, 0, 10);
}

unsigned long strtoul(const char *text, char **endptr, int base) {
    return (unsigned long)strtol(text, endptr, base);
}

unsigned long long strtoull(const char *text, char **endptr, int base) {
    return (unsigned long long)strtoll(text, endptr, base);
}

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
    const char *base = bsdgame_basename(progname);

    strlcpy(g_progname_storage, base, sizeof(g_progname_storage));
    __progname = g_progname_storage;
}

const char *getprogname(void) {
    return (__progname && __progname[0] != '\0') ? __progname : "bsdgame";
}

char *setlocale(int category, const char *locale) {
    (void)category;
    (void)locale;
    return g_bsdgame_locale_name;
}

struct lconv *localeconv(void) {
    return &g_bsdgame_locale;
}

long long strtonum(const char *text, long long minval, long long maxval,
                   const char **errstrp) {
    char *end = 0;
    long long value;

    if (errstrp) {
        *errstrp = 0;
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

uint32_t arc4random(void) {
    return bsdgame_next_random();
}

long random(void) {
    return (long)(bsdgame_next_random() & 0x7fffffffu);
}

void srandom_deterministic(unsigned int seed) {
    g_bsdgame_random_state = seed != 0u ? seed : 1u;
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

void arc4random_buf(void *buf, size_t size) {
    uint8_t *out = (uint8_t *)buf;
    uint32_t value = 0u;
    int remaining = 0;

    if (!out) {
        return;
    }
    while (size > 0u) {
        if (remaining == 0) {
            value = arc4random();
            remaining = 4;
        }
        *out++ = (uint8_t)(value & 0xffu);
        value >>= 8;
        --remaining;
        --size;
    }
}

size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t src_len = 0u;

    if (!src) {
        if (dst && size > 0u) {
            dst[0] = '\0';
        }
        return 0u;
    }
    while (src[src_len] != '\0') {
        ++src_len;
    }
    if (dst && size > 0u) {
        size_t copy_len = src_len;

        if (copy_len >= size) {
            copy_len = size - 1u;
        }
        for (size_t i = 0; i < copy_len; ++i) {
            dst[i] = src[i];
        }
        dst[copy_len] = '\0';
    }
    return src_len;
}

size_t strlcat(char *dst, const char *src, size_t size) {
    size_t dst_len = 0u;
    size_t src_len = 0u;

    while (dst && dst_len < size && dst[dst_len] != '\0') {
        ++dst_len;
    }
    while (src && src[src_len] != '\0') {
        ++src_len;
    }
    if (!dst || size == 0u || dst_len >= size) {
        return dst_len + src_len;
    }
    if (src) {
        size_t copy_len = size - dst_len - 1u;
        size_t i;

        for (i = 0; i < copy_len && src[i] != '\0'; ++i) {
            dst[dst_len + i] = src[i];
        }
        dst[dst_len + i] = '\0';
    }
    return dst_len + src_len;
}

char *strsep(char **stringp, const char *delim) {
    char *start;
    char *cursor;

    if (!stringp || !*stringp) {
        return 0;
    }
    start = *stringp;
    cursor = start;
    while (*cursor != '\0') {
        if (strchr(delim, *cursor)) {
            *cursor = '\0';
            *stringp = cursor + 1;
            return start;
        }
        ++cursor;
    }
    *stringp = 0;
    return start;
}

sig_t signal(int sig, sig_t handler) {
    sig_t previous = 0;

    if (sig >= 0 && sig < (int)(sizeof(g_signal_handlers) / sizeof(g_signal_handlers[0]))) {
        previous = g_signal_handlers[sig];
        g_signal_handlers[sig] = handler;
    }
    return previous;
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact) {
    if (sig < 0 || sig >= (int)(sizeof(g_signal_handlers) / sizeof(g_signal_handlers[0]))) {
        return -1;
    }
    if (oldact) {
        oldact->sa_handler = g_signal_handlers[sig];
        oldact->sa_mask = 0u;
        oldact->sa_flags = 0;
    }
    if (act) {
        g_signal_handlers[sig] = act->sa_handler;
    }
    return 0;
}

int sigemptyset(sigset_t *set) {
    if (!set) {
        return -1;
    }
    *set = 0u;
    return 0;
}

int sigaddset(sigset_t *set, int sig) {
    if (!set || sig < 0 || sig >= (int)(sizeof(sigset_t) * 8u)) {
        return -1;
    }
    *set |= ((sigset_t)1u << sig);
    return 0;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
    (void)how;
    (void)set;
    if (oldset) {
        *oldset = 0u;
    }
    return 0;
}

unsigned int alarm(unsigned int seconds) {
    (void)seconds;
    return 0u;
}

int kill(pid_t pid, int sig) {
    (void)pid;
    (void)sig;
    return 0;
}

void warn(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    bsdgame_write_message(stderr, getprogname(), 0, fmt, ap, 0);
    va_end(ap);
}

void vwarn(const char *fmt, va_list ap) {
    bsdgame_write_message(stderr, getprogname(), 0, fmt, ap, 0);
}

void warnc(int code, const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    bsdgame_write_message(stderr, getprogname(), code, fmt, ap, 1);
    va_end(ap);
}

void vwarnc(int code, const char *fmt, va_list ap) {
    bsdgame_write_message(stderr, getprogname(), code, fmt, ap, 1);
}

void warnx(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    bsdgame_write_message(stderr, getprogname(), 0, fmt, ap, 0);
    va_end(ap);
}

void vwarnx(const char *fmt, va_list ap) {
    bsdgame_write_message(stderr, getprogname(), 0, fmt, ap, 0);
}

void err(int eval, const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    bsdgame_write_message(stderr, getprogname(), eval, fmt, ap, 1);
    va_end(ap);
    vibe_bsdgame_exit(eval);
}

void verr(int eval, const char *fmt, va_list ap) {
    bsdgame_write_message(stderr, getprogname(), eval, fmt, ap, 1);
    vibe_bsdgame_exit(eval);
}

void errc(int eval, int code, const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    bsdgame_write_message(stderr, getprogname(), code, fmt, ap, 1);
    va_end(ap);
    vibe_bsdgame_exit(eval);
}

void verrc(int eval, int code, const char *fmt, va_list ap) {
    bsdgame_write_message(stderr, getprogname(), code, fmt, ap, 1);
    vibe_bsdgame_exit(eval);
}

void errx(int eval, const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    bsdgame_write_message(stderr, getprogname(), 0, fmt, ap, 0);
    va_end(ap);
    vibe_bsdgame_exit(eval);
}

void verrx(int eval, const char *fmt, va_list ap) {
    bsdgame_write_message(stderr, getprogname(), 0, fmt, ap, 0);
    vibe_bsdgame_exit(eval);
}

char *optarg;
int optind = 1;
int opterr = 1;
int optopt = '?';
int optreset = 0;

int getopt(int argc, char * const argv[], const char *optstring) {
    static int position = 1;
    const char *opt_decl;
    char option;

    if (optreset) {
        optreset = 0;
        position = 1;
        optind = 1;
    }
    optarg = 0;
    if (optind >= argc || !argv || !argv[optind]) {
        return -1;
    }
    if (argv[optind][0] != '-' || argv[optind][1] == '\0') {
        return -1;
    }
    if (argv[optind][1] == '-' && argv[optind][2] == '\0') {
        ++optind;
        position = 1;
        return -1;
    }

    option = argv[optind][position++];
    opt_decl = strchr(optstring, option);
    if (!opt_decl) {
        optopt = option;
        if (argv[optind][position] == '\0') {
            ++optind;
            position = 1;
        }
        return '?';
    }

    if (opt_decl[1] == ':') {
        if (argv[optind][position] != '\0') {
            optarg = &argv[optind][position];
            ++optind;
            position = 1;
        } else if (optind + 1 < argc) {
            optarg = argv[++optind];
            ++optind;
            position = 1;
        } else {
            optopt = option;
            ++optind;
            position = 1;
            return (optstring[0] == ':') ? ':' : '?';
        }
    } else if (argv[optind][position] == '\0') {
        ++optind;
        position = 1;
    }

    return option;
}

int fileno(FILE *stream) {
    if (stream == stdin) {
        return 0;
    }
    if (stream == stdout) {
        return 1;
    }
    if (stream == stderr) {
        return 2;
    }
    return -1;
}

char *strrchr(const char *text, int c) {
    char *result = 0;

    while (text && *text != '\0') {
        if (*text == (char)c) {
            result = (char *)text;
        }
        ++text;
    }
    if (text && *text == (char)c) {
        return (char *)text;
    }
    return result;
}

char *strstr(const char *text, const char *needle) {
    if (!text || !needle) {
        return 0;
    }
    if (*needle == '\0') {
        return (char *)text;
    }
    while (*text != '\0') {
        const char *lhs = text;
        const char *rhs = needle;

        while (*lhs != '\0' && *rhs != '\0' && *lhs == *rhs) {
            ++lhs;
            ++rhs;
        }
        if (*rhs == '\0') {
            return (char *)text;
        }
        ++text;
    }
    return 0;
}

char *strcasestr(const char *text, const char *needle) {
    if (!text || !needle) {
        return 0;
    }
    if (*needle == '\0') {
        return (char *)text;
    }
    while (*text != '\0') {
        const char *lhs = text;
        const char *rhs = needle;

        while (*lhs != '\0' && *rhs != '\0') {
            int lc = tolower((unsigned char)*lhs);
            int rc = tolower((unsigned char)*rhs);

            if (lc != rc) {
                break;
            }
            ++lhs;
            ++rhs;
        }
        if (*rhs == '\0') {
            return (char *)text;
        }
        ++text;
    }
    return 0;
}

size_t strspn(const char *text, const char *accept) {
    size_t count = 0u;

    while (text && text[count] != '\0' && strchr(accept, text[count]) != 0) {
        ++count;
    }
    return count;
}

size_t strcspn(const char *text, const char *reject) {
    size_t count = 0u;

    while (text && text[count] != '\0' && strchr(reject, text[count]) == 0) {
        ++count;
    }
    return count;
}

char *strtok_r(char *text, const char *delim, char **saveptr) {
    char *start;

    if (!text) {
        text = saveptr ? *saveptr : 0;
    }
    if (!text || !delim || !saveptr) {
        return 0;
    }
    text += strspn(text, delim);
    if (*text == '\0') {
        *saveptr = text;
        return 0;
    }
    start = text;
    text += strcspn(text, delim);
    if (*text != '\0') {
        *text++ = '\0';
    }
    *saveptr = text;
    return start;
}

char *strtok(char *text, const char *delim) {
    static char *g_saveptr = 0;

    return strtok_r(text, delim, &g_saveptr);
}

char *strdup(const char *text) {
    size_t len;
    char *copy;

    if (!text) {
        return 0;
    }
    len = strlen(text) + 1u;
    copy = malloc(len);
    if (!copy) {
        return 0;
    }
    memcpy(copy, text, len);
    return copy;
}

char *strndup(const char *text, size_t max_len) {
    size_t len = 0u;
    char *copy;

    if (!text) {
        return 0;
    }
    while (text[len] != '\0' && len < max_len) {
        ++len;
    }
    copy = malloc(len + 1u);
    if (!copy) {
        return 0;
    }
    memcpy(copy, text, len);
    copy[len] = '\0';
    return copy;
}

int strcasecmp(const char *lhs, const char *rhs) {
    unsigned char lch;
    unsigned char rch;

    if (!lhs) {
        lhs = "";
    }
    if (!rhs) {
        rhs = "";
    }
    do {
        lch = (unsigned char)tolower((unsigned char)*lhs++);
        rch = (unsigned char)tolower((unsigned char)*rhs++);
        if (lch != rch) {
            return (int)lch - (int)rch;
        }
    } while (lch != '\0');
    return 0;
}

int strncasecmp(const char *lhs, const char *rhs, size_t n) {
    unsigned char lch;
    unsigned char rch;

    if (!lhs) {
        lhs = "";
    }
    if (!rhs) {
        rhs = "";
    }
    while (n-- > 0u) {
        lch = (unsigned char)tolower((unsigned char)*lhs++);
        rch = (unsigned char)tolower((unsigned char)*rhs++);
        if (lch != rch) {
            return (int)lch - (int)rch;
        }
        if (lch == '\0') {
            break;
        }
    }
    return 0;
}

double strtod(const char *text, char **endptr) {
    const char *cursor = text;
    double result = 0.0;
    double fraction = 0.1;
    int negative = 0;

    if (!text) {
        if (endptr) {
            *endptr = 0;
        }
        return 0.0;
    }

    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r') {
        ++cursor;
    }
    if (*cursor == '-') {
        negative = 1;
        ++cursor;
    } else if (*cursor == '+') {
        ++cursor;
    }

    while (*cursor >= '0' && *cursor <= '9') {
        result = (result * 10.0) + (double)(*cursor - '0');
        ++cursor;
    }
    if (*cursor == '.') {
        ++cursor;
        while (*cursor >= '0' && *cursor <= '9') {
            result += (double)(*cursor - '0') * fraction;
            fraction *= 0.1;
            ++cursor;
        }
    }
    if (*cursor == 'e' || *cursor == 'E') {
        int exponent = 0;
        int exponent_negative = 0;

        ++cursor;
        if (*cursor == '-') {
            exponent_negative = 1;
            ++cursor;
        } else if (*cursor == '+') {
            ++cursor;
        }
        while (*cursor >= '0' && *cursor <= '9') {
            exponent = (exponent * 10) + (*cursor - '0');
            ++cursor;
        }
        while (exponent-- > 0) {
            result *= exponent_negative ? 0.1 : 10.0;
        }
    }

    if (endptr) {
        *endptr = (char *)cursor;
    }
    return negative ? -result : result;
}

struct tm *gmtime(const time_t *timep) {
    if (!timep) {
        return 0;
    }
    bsdgame_seconds_to_tm(*timep, &g_bsdgame_gmtime);
    return &g_bsdgame_gmtime;
}

struct tm *localtime(const time_t *timep) {
    if (!timep) {
        return 0;
    }
    bsdgame_seconds_to_tm(*timep, &g_bsdgame_localtime);
    return &g_bsdgame_localtime;
}

int clock_gettime(clockid_t clock_id, struct timespec *ts) {
    unsigned long long millis;

    (void)clock_id;
    if (!ts) {
        return -1;
    }
    millis = vibe_app_millis();
    ts->tv_sec = (time_t)(millis / 1000ull);
    ts->tv_nsec = (long)((millis % 1000ull) * 1000000ull);
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    unsigned long long total_ms = 0ull;

    if (!req) {
        return -1;
    }
    if (req->tv_sec > 0) {
        total_ms += (unsigned long long)req->tv_sec * 1000ull;
    }
    if (req->tv_nsec > 0) {
        total_ms += (unsigned long long)((req->tv_nsec + 999999L) / 1000000L);
    }
    if (total_ms > 0ull) {
        vibe_app_sleep_ms((unsigned int)total_ms);
    }
    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    return 0;
}

time_t mktime(struct tm *tm) {
    long long days = 0ll;
    int year;
    int month;

    if (!tm) {
        return (time_t)-1;
    }
    year = tm->tm_year + 1900;
    for (int y = 1970; y < year; ++y) {
        days += (long long)(bsdgame_is_leap_year(y) ? 366 : 365);
    }
    for (month = 0; month < tm->tm_mon; ++month) {
        days += (long long)bsdgame_days_in_month(year, month);
    }
    days += (long long)(tm->tm_mday - 1);

    tm->tm_yday = 0;
    for (month = 0; month < tm->tm_mon; ++month) {
        tm->tm_yday += bsdgame_days_in_month(year, month);
    }
    tm->tm_yday += tm->tm_mday - 1;
    tm->tm_wday = (int)((days + 4ll) % 7ll);
    if (tm->tm_wday < 0) {
        tm->tm_wday += 7;
    }
    tm->tm_isdst = 0;

    return (time_t)((days * 86400ll) +
                    ((long long)tm->tm_hour * 3600ll) +
                    ((long long)tm->tm_min * 60ll) +
                    (long long)tm->tm_sec);
}

char *ctime(const time_t *timep) {
    static char buffer[32];
    struct tm *tm = localtime(timep);
    static const char *const weekdays[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char *const months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    if (!tm) {
        return 0;
    }
    snprintf(buffer, sizeof(buffer),
             "%s %s %02d %02d:%02d:%02d %04d\n",
             weekdays[(tm->tm_wday >= 0 && tm->tm_wday < 7) ? tm->tm_wday : 0],
             months[(tm->tm_mon >= 0 && tm->tm_mon < 12) ? tm->tm_mon : 0],
             tm->tm_mday,
             tm->tm_hour,
             tm->tm_min,
             tm->tm_sec,
             tm->tm_year + 1900);
    return buffer;
}

static int bsdgame_append_char(char *dst, size_t size, size_t *used, char c) {
    if (*used + 1u >= size) {
        return 0;
    }
    dst[*used] = c;
    ++(*used);
    dst[*used] = '\0';
    return 1;
}

static int bsdgame_append_text(char *dst, size_t size, size_t *used, const char *text) {
    while (text && *text != '\0') {
        if (!bsdgame_append_char(dst, size, used, *text++)) {
            return 0;
        }
    }
    return 1;
}

static int bsdgame_append_number(char *dst, size_t size, size_t *used,
                                 int value, int width, char pad) {
    char buf[16];
    int len = 0;
    int v = value;

    if (v == 0) {
        buf[len++] = '0';
    } else {
        if (v < 0) {
            if (!bsdgame_append_char(dst, size, used, '-')) {
                return 0;
            }
            v = -v;
        }
        while (v > 0 && len < (int)sizeof(buf)) {
            buf[len++] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
    while (len < width) {
        if (!bsdgame_append_char(dst, size, used, pad)) {
            return 0;
        }
        --width;
    }
    while (len-- > 0) {
        if (!bsdgame_append_char(dst, size, used, buf[len])) {
            return 0;
        }
    }
    return 1;
}

size_t strftime(char *dst, size_t size, const char *format,
                const struct tm *tm) {
    static const char *const weekdays[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char *const months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    size_t used = 0u;

    if (!dst || size == 0u || !format || !tm) {
        return 0u;
    }
    dst[0] = '\0';
    while (*format != '\0') {
        if (*format != '%') {
            if (!bsdgame_append_char(dst, size, &used, *format++)) {
                return 0u;
            }
            continue;
        }
        ++format;
        switch (*format) {
        case '%':
            if (!bsdgame_append_char(dst, size, &used, '%')) {
                return 0u;
            }
            break;
        case 'a':
            if (!bsdgame_append_text(dst, size, &used, weekdays[tm->tm_wday % 7])) {
                return 0u;
            }
            break;
        case 'b':
        case 'h':
            if (!bsdgame_append_text(dst, size, &used, months[tm->tm_mon % 12])) {
                return 0u;
            }
            break;
        case 'Y':
            if (!bsdgame_append_number(dst, size, &used, tm->tm_year + 1900, 4, '0')) {
                return 0u;
            }
            break;
        case 'm':
            if (!bsdgame_append_number(dst, size, &used, tm->tm_mon + 1, 2, '0')) {
                return 0u;
            }
            break;
        case 'd':
            if (!bsdgame_append_number(dst, size, &used, tm->tm_mday, 2, '0')) {
                return 0u;
            }
            break;
        case 'e':
            if (!bsdgame_append_number(dst, size, &used, tm->tm_mday, 2, ' ')) {
                return 0u;
            }
            break;
        case 'H':
            if (!bsdgame_append_number(dst, size, &used, tm->tm_hour, 2, '0')) {
                return 0u;
            }
            break;
        case 'M':
            if (!bsdgame_append_number(dst, size, &used, tm->tm_min, 2, '0')) {
                return 0u;
            }
            break;
        case 'S':
            if (!bsdgame_append_number(dst, size, &used, tm->tm_sec, 2, '0')) {
                return 0u;
            }
            break;
        case 'R':
            if (!bsdgame_append_number(dst, size, &used, tm->tm_hour, 2, '0') ||
                !bsdgame_append_char(dst, size, &used, ':') ||
                !bsdgame_append_number(dst, size, &used, tm->tm_min, 2, '0')) {
                return 0u;
            }
            break;
        case 'T':
            if (!bsdgame_append_number(dst, size, &used, tm->tm_hour, 2, '0') ||
                !bsdgame_append_char(dst, size, &used, ':') ||
                !bsdgame_append_number(dst, size, &used, tm->tm_min, 2, '0') ||
                !bsdgame_append_char(dst, size, &used, ':') ||
                !bsdgame_append_number(dst, size, &used, tm->tm_sec, 2, '0')) {
                return 0u;
            }
            break;
        case 'F':
            if (!bsdgame_append_number(dst, size, &used, tm->tm_year + 1900, 4, '0') ||
                !bsdgame_append_char(dst, size, &used, '-') ||
                !bsdgame_append_number(dst, size, &used, tm->tm_mon + 1, 2, '0') ||
                !bsdgame_append_char(dst, size, &used, '-') ||
                !bsdgame_append_number(dst, size, &used, tm->tm_mday, 2, '0')) {
                return 0u;
            }
            break;
        case 'Z':
            if (!bsdgame_append_text(dst, size, &used, "UTC")) {
                return 0u;
            }
            break;
        default:
            if (!bsdgame_append_char(dst, size, &used, '%') ||
                !bsdgame_append_char(dst, size, &used, *format)) {
                return 0u;
            }
            break;
        }
        if (*format != '\0') {
            ++format;
        }
    }
    return used;
}

char *getlogin(void) {
    static char login[32];
    const char *value = getenv("USER");

    if (!value || value[0] == '\0') {
        value = getenv("USERNAME");
    }
    if (!value || value[0] == '\0') {
        value = "player";
    }
    strlcpy(login, value, sizeof(login));
    return login;
}

int setvbuf(FILE *stream, char *buf, int mode, size_t size) {
    (void)stream;
    (void)buf;
    (void)mode;
    (void)size;
    return 0;
}

int fpurge(FILE *stream) {
    (void)stream;
    return 0;
}

static void bsdgame_swap_bytes(unsigned char *lhs, unsigned char *rhs, size_t size) {
    while (size-- > 0u) {
        unsigned char tmp = *lhs;

        *lhs++ = *rhs;
        *rhs++ = tmp;
    }
}

void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *)) {
    unsigned char *bytes = (unsigned char *)base;
    unsigned char *tmp;

    if (!base || !compar || nmemb < 2u || size == 0u) {
        return;
    }
    tmp = (unsigned char *)malloc(size);
    if (!tmp) {
        return;
    }
    for (size_t i = 1u; i < nmemb; ++i) {
        size_t j = i;

        memcpy(tmp, bytes + (i * size), size);
        while (j > 0u &&
               compar(tmp, bytes + ((j - 1u) * size)) < 0) {
            memcpy(bytes + (j * size),
                   bytes + ((j - 1u) * size),
                   size);
            --j;
        }
        memcpy(bytes + (j * size), tmp, size);
    }
    free(tmp);
}

char *vis(char *dst, int c, int flag, int nextc) {
    unsigned char uc = (unsigned char)c;

    (void)nextc;
    if (!dst) {
        return 0;
    }
    if (uc >= 32u && uc < 127u && uc != '\\') {
        dst[0] = (char)uc;
        dst[1] = '\0';
        return dst;
    }
    if (uc == '\n') {
        if (flag & VIS_NOSLASH) {
            dst[0] = '^';
            dst[1] = 'J';
            dst[2] = '\0';
        } else {
            dst[0] = '\\';
            dst[1] = 'n';
            dst[2] = '\0';
        }
        return dst;
    }
    if (uc == '\t') {
        if (flag & VIS_NOSLASH) {
            dst[0] = '^';
            dst[1] = 'I';
            dst[2] = '\0';
        } else {
            dst[0] = '\\';
            dst[1] = 't';
            dst[2] = '\0';
        }
        return dst;
    }
    if (uc < 32u || uc == 127u) {
        dst[0] = '^';
        dst[1] = (uc == 127u) ? '?' : (char)('@' + uc);
        dst[2] = '\0';
        return dst;
    }
    if (flag & VIS_NOSLASH) {
        dst[0] = '?';
        dst[1] = '\0';
        return dst;
    }
    dst[0] = '\\';
    dst[1] = (char)('0' + ((uc >> 6) & 0x7u));
    dst[2] = (char)('0' + ((uc >> 3) & 0x7u));
    dst[3] = (char)('0' + (uc & 0x7u));
    dst[4] = '\0';
    return dst;
}

static double bsdgame_fabs(double x) {
    return x < 0.0 ? -x : x;
}

static double bsdgame_floor(double x) {
    long long whole = (long long)x;

    if ((double)whole > x) {
        --whole;
    }
    return (double)whole;
}

static double bsdgame_ceil(double x) {
    long long whole = (long long)x;

    if ((double)whole < x) {
        ++whole;
    }
    return (double)whole;
}

static double bsdgame_wrap_pi(double x) {
    while (x > M_PI) {
        x -= 2.0 * M_PI;
    }
    while (x < -M_PI) {
        x += 2.0 * M_PI;
    }
    return x;
}

double fabs(double x) {
    return bsdgame_fabs(x);
}

double floor(double x) {
    return bsdgame_floor(x);
}

double ceil(double x) {
    return bsdgame_ceil(x);
}

double round(double x) {
    if (x >= 0.0) {
        return bsdgame_floor(x + 0.5);
    }
    return bsdgame_ceil(x - 0.5);
}

long lround(double x) {
    return (long)round(x);
}

double fmod(double x, double y) {
    long long quotient;

    if (y == 0.0) {
        return 0.0;
    }
    quotient = (long long)(x / y);
    return x - ((double)quotient * y);
}

double ldexp(double x, int exp) {
    while (exp > 0) {
        x *= 2.0;
        --exp;
    }
    while (exp < 0) {
        x *= 0.5;
        ++exp;
    }
    return x;
}

double sqrt(double x) {
    double guess;

    if (x <= 0.0) {
        return 0.0;
    }
    guess = (x > 1.0) ? x : 1.0;
    for (int i = 0; i < 16; ++i) {
        guess = 0.5 * (guess + (x / guess));
    }
    return guess;
}

double sin(double x) {
    double term;
    double sum;

    x = bsdgame_wrap_pi(x);
    term = x;
    sum = x;
    for (int i = 1; i < 8; ++i) {
        double denom = (2.0 * (double)i) * ((2.0 * (double)i) + 1.0);

        term *= -(x * x) / denom;
        sum += term;
    }
    return sum;
}

double cos(double x) {
    double term = 1.0;
    double sum = 1.0;

    x = bsdgame_wrap_pi(x);
    for (int i = 1; i < 8; ++i) {
        double denom = ((2.0 * (double)i) - 1.0) * (2.0 * (double)i);

        term *= -(x * x) / denom;
        sum += term;
    }
    return sum;
}

double tan(double x) {
    double denom = cos(x);

    if (denom == 0.0) {
        return 0.0;
    }
    return sin(x) / denom;
}

double atan(double x) {
    double xx;
    double term;
    double sum;

    if (x > 1.0) {
        return (M_PI / 2.0) - atan(1.0 / x);
    }
    if (x < -1.0) {
        return -(M_PI / 2.0) - atan(1.0 / x);
    }
    xx = x * x;
    term = x;
    sum = x;
    for (int i = 1; i < 16; ++i) {
        term *= -xx;
        sum += term / ((2.0 * (double)i) + 1.0);
    }
    return sum;
}

double atan2(double y, double x) {
    if (x > 0.0) {
        return atan(y / x);
    }
    if (x < 0.0 && y >= 0.0) {
        return atan(y / x) + M_PI;
    }
    if (x < 0.0 && y < 0.0) {
        return atan(y / x) - M_PI;
    }
    if (x == 0.0 && y > 0.0) {
        return M_PI / 2.0;
    }
    if (x == 0.0 && y < 0.0) {
        return -M_PI / 2.0;
    }
    return 0.0;
}

double acos(double x) {
    if (x <= -1.0) {
        return M_PI;
    }
    if (x >= 1.0) {
        return 0.0;
    }
    return atan2(sqrt(1.0 - (x * x)), x);
}

double exp(double x) {
    static const double ln2 = 0.69314718055994530942;
    double scale = 1.0;
    double sum = 1.0;
    double term = 1.0;

    while (x > ln2) {
        x -= ln2;
        scale *= 2.0;
    }
    while (x < -ln2) {
        x += ln2;
        scale *= 0.5;
    }
    for (int i = 1; i < 18; ++i) {
        term *= x / (double)i;
        sum += term;
    }
    return sum * scale;
}

double log(double x) {
    static const double ln2 = 0.69314718055994530942;
    double y;
    double y2;
    double term;
    double sum = 0.0;
    int exponent = 0;

    if (x <= 0.0) {
        return 0.0 / 0.0;
    }
    while (x > 1.5) {
        x *= 0.5;
        ++exponent;
    }
    while (x < 0.75) {
        x *= 2.0;
        --exponent;
    }
    y = (x - 1.0) / (x + 1.0);
    y2 = y * y;
    term = y;
    for (int i = 1; i < 24; i += 2) {
        sum += term / (double)i;
        term *= y2;
    }
    return (2.0 * sum) + ((double)exponent * ln2);
}

double pow(double x, double y) {
    long long iy = (long long)y;

    if ((double)iy == y) {
        double result = 1.0;
        int negative = 0;

        if (iy < 0) {
            negative = 1;
            iy = -iy;
        }
        while (iy > 0) {
            if ((iy & 1ll) != 0ll) {
                result *= x;
            }
            x *= x;
            iy >>= 1;
        }
        return negative ? (1.0 / result) : result;
    }
    if (x <= 0.0) {
        return 0.0;
    }
    return exp(y * log(x));
}
