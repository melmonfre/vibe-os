#ifndef VIBE_STDIO_H
#define VIBE_STDIO_H

typedef unsigned int size_t;

#define EOF (-1)

typedef struct FILE {
    int handle;
} FILE;

/* Standard file handles */
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int sprintf(char *str, const char *fmt, ...);
int snprintf(char *str, size_t size, const char *fmt, ...);

int vprintf(const char *fmt, void *ap);
int vfprintf(FILE *f, const char *fmt, void *ap);
int vsprintf(char *str, const char *fmt, void *ap);
int vsnprintf(char *str, size_t size, const char *fmt, void *ap);

FILE *fopen(const char *filename, const char *mode);
FILE *fdopen(int fd, const char *mode);
int fclose(FILE *stream);
int fflush(FILE *stream);

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);

int fgetc(FILE *stream);
int fputc(int c, FILE *stream);
int fputs(const char *s, FILE *stream);
int puts(const char *s);

void clearerr(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);

int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);

int getchar(void);
int putchar(int c);

#endif
