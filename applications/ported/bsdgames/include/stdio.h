#ifndef VIBE_BSDGAME_STDIO_H
#define VIBE_BSDGAME_STDIO_H

#include <lang/include/vibe_app_runtime.h>

#ifndef EOF
#define EOF (-1)
#endif

#ifndef BUFSIZ
#define BUFSIZ 1024
#endif

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

#ifndef _IONBF
#define _IONBF 2
#endif

#ifndef _IOFBF
#define _IOFBF 0
#endif

#ifndef _IOLBF
#define _IOLBF 1
#endif

int fileno(FILE *stream);
int setvbuf(FILE *stream, char *buf, int mode, size_t size);
int fpurge(FILE *stream);
int ungetc(int c, FILE *stream);
int sscanf(const char *str, const char *fmt, ...);
int vsscanf(const char *str, const char *fmt, va_list ap);
int fscanf(FILE *stream, const char *fmt, ...);
int vfscanf(FILE *stream, const char *fmt, va_list ap);
int scanf(const char *fmt, ...);
int vscanf(const char *fmt, va_list ap);
int getw(FILE *stream);
int putw(int value, FILE *stream);
ssize_t getdelim(char **lineptr, size_t *n, int delim, FILE *stream);
ssize_t getline(char **lineptr, size_t *n, FILE *stream);
int vasprintf(char **strp, const char *fmt, va_list ap);
int asprintf(char **strp, const char *fmt, ...);
void perror(const char *s);

#endif
