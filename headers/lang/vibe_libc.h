#ifndef VIBE_LIBC_H
#define VIBE_LIBC_H

/* Bare-metal libc types - NO system headers! */
typedef unsigned int size_t;
typedef int ssize_t;
typedef int ptrdiff_t;
typedef unsigned char uint8_t;
typedef signed char int8_t;
typedef unsigned short uint16_t;
typedef signed short int16_t;
typedef unsigned int uint32_t;
typedef signed int int32_t;
typedef unsigned long long uint64_t;
typedef signed long long int64_t;
typedef char *va_list;

typedef int FILE;
#define stdin ((FILE*)-1)
#define stdout ((FILE*)1)
#define stderr ((FILE*)2)

#define NULL ((void*)0)
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* Memory management */
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

/* String functions */
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
char *strtok(char *s, const char *delim);
char *strtok_r(char *s, const char *delim, char **save_ptr);
char *strdup(const char *s);
char *strndup(const char *s, size_t n);

/* Additional string functions from glibc */
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);

/* Conversion functions */
int atoi(const char *nptr);
long atol(const char *nptr);
long long atoll(const char *nptr);
long strtol(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
double strtod(const char *nptr, char **endptr);
double atof(const char *nptr);

/* I/O functions */
int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int sprintf(char *str, const char *fmt, ...);
int snprintf(char *str, size_t size, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int vsprintf(char *str, const char *fmt, va_list ap);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);

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

/* Math/sorting */
typedef int (*__compar_fn_t)(const void *, const void *);
void *bsearch(const void *key, const void *base,
              size_t nmemb, size_t size, __compar_fn_t compar);
void qsort(void *base, size_t nmemb, size_t size, __compar_fn_t compar);
int abs(int j);
long labs(long j);
long long llabs(long long j);

/* Process */
void exit(int status);
void abort(void);
int atexit(void (*func)(void));
unsigned int sleep(unsigned int seconds);
int usleep(unsigned int microseconds);

#endif
