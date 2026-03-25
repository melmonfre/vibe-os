#ifndef VIBE_BSDGAME_ERR_H
#define VIBE_BSDGAME_ERR_H

#include <stdarg.h>

#ifndef __dead
#define __dead __attribute__((noreturn))
#endif

void warn(const char *fmt, ...);
void vwarn(const char *fmt, va_list ap);
void warnc(int code, const char *fmt, ...);
void vwarnc(int code, const char *fmt, va_list ap);
void warnx(const char *fmt, ...);
void vwarnx(const char *fmt, va_list ap);
__dead void err(int eval, const char *fmt, ...);
__dead void verr(int eval, const char *fmt, va_list ap);
__dead void errc(int eval, int code, const char *fmt, ...);
__dead void verrc(int eval, int code, const char *fmt, va_list ap);
__dead void errx(int eval, const char *fmt, ...);
__dead void verrx(int eval, const char *fmt, va_list ap);

#endif
