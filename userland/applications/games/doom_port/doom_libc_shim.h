#ifndef VIBE_DOOM_LIBC_SHIM_H
#define VIBE_DOOM_LIBC_SHIM_H

#include <userland/lua/include/lua_port.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>

extern FILE *stdout;
extern FILE *stderr;
extern char *sndserver_filename;

int atoi(const char *s);
char *strcat(char *dst, const char *src);
int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, size_t n);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);
int snprintf(char *str, size_t size, const char *fmt, ...);
int vsprintf(char *str, const char *fmt, va_list ap);
int sprintf(char *str, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int printf(const char *fmt, ...);
int vfprintf(FILE *stream, const char *fmt, va_list ap);
int fprintf(FILE *stream, const char *fmt, ...);
int putchar(int c);
int getchar(void);
int puts(const char *s);
int fputc(int c, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fflush(FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void setbuf(FILE *stream, char *buf);
int sscanf(const char *str, const char *fmt, ...);
int fscanf(FILE *stream, const char *fmt, ...);
int access(const char *path, int mode);
int mkdir(const char *path, mode_t mode);
int open(const char *path, int flags, ...);
int close(int fd);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
off_t lseek(int fd, off_t offset, int whence);
int fstat(int fd, struct stat *buf);
void exit(int status) __attribute__((noreturn));

#endif
