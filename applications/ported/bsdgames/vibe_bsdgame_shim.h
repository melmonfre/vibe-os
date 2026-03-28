#ifndef VIBE_BSDGAME_SHIM_H
#define VIBE_BSDGAME_SHIM_H

#ifndef __BSD_VISIBLE
#define __BSD_VISIBLE 1
#endif

#ifndef __POSIX_VISIBLE
#define __POSIX_VISIBLE 200809
#endif

#include <compat_defs.h>
#include <stdint.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <getopt.h>
#include <locale.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <vis.h>

typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;
typedef long long quad_t;
typedef unsigned long long u_quad_t;
typedef uint8_t u_int8_t;
typedef uint32_t u_int32_t;
typedef uint64_t u_int64_t;

#ifndef BIG_ENDIAN
#define BIG_ENDIAN 4321
#endif

#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN 1234
#endif

#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif

#ifndef __predict_false
#define __predict_false(expr) (expr)
#endif

#ifndef __predict_true
#define __predict_true(expr) (expr)
#endif

#ifndef isblank
int isblank(int c);
#endif

extern char *__progname;

int pledge(const char *promises, const char *execpromises);
int unveil(const char *path, const char *permissions);
void setprogname(const char *progname);
const char *getprogname(void);
long long strtonum(const char *text, long long minval, long long maxval,
                   const char **errstrp);
uint32_t arc4random(void);
uint32_t arc4random_uniform(uint32_t upper_bound);
void arc4random_buf(void *buf, size_t size);
char *strncpy(char *dst, const char *src, size_t n);
size_t strlcpy(char *dst, const char *src, size_t size);
size_t strlcat(char *dst, const char *src, size_t size);
char *strsep(char **stringp, const char *delim);
void warn(const char *fmt, ...);
void vwarn(const char *fmt, va_list ap);
void warnc(int code, const char *fmt, ...);
void vwarnc(int code, const char *fmt, va_list ap);
void warnx(const char *fmt, ...);
void vwarnx(const char *fmt, va_list ap);
void err(int eval, const char *fmt, ...);
void verr(int eval, const char *fmt, va_list ap);
void errc(int eval, int code, const char *fmt, ...);
void verrc(int eval, int code, const char *fmt, va_list ap);
void errx(int eval, const char *fmt, ...);
void verrx(int eval, const char *fmt, va_list ap);
void vibe_bsdgame_exit(int status);
int bsdgame_printf(const char *fmt, ...);
int bsdgame_fprintf(FILE *stream, const char *fmt, ...);
int bsdgame_vprintf(const char *fmt, va_list ap);
int bsdgame_vfprintf(FILE *stream, const char *fmt, va_list ap);
int bsdgame_sprintf(char *dst, const char *fmt, ...);
int bsdgame_snprintf(char *dst, size_t size, const char *fmt, ...);
int bsdgame_vsprintf(char *dst, const char *fmt, va_list ap);
int bsdgame_vsnprintf(char *dst, size_t size, const char *fmt, va_list ap);
int delay_output(int ms);

#ifndef VIBE_BSDGAME_NO_EXIT_REMAP
#define exit vibe_bsdgame_exit
#define _exit vibe_bsdgame_exit
#endif

#define printf bsdgame_printf
#define fprintf bsdgame_fprintf
#define vprintf bsdgame_vprintf
#define vfprintf bsdgame_vfprintf
#define sprintf bsdgame_sprintf
#define snprintf bsdgame_snprintf
#define vsprintf bsdgame_vsprintf
#define vsnprintf bsdgame_vsnprintf

#endif
