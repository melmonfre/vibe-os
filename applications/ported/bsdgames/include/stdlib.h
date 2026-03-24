#ifndef VIBE_BSDGAME_STDLIB_H
#define VIBE_BSDGAME_STDLIB_H

#include <lang/include/vibe_app_runtime.h>
#include <stdint.h>

#define EXIT_FAILURE 1
#define EXIT_SUCCESS 0
#define RAND_MAX 0x7fffffff
#define MB_CUR_MAX 1

int abs(int value);
long atol(const char *text);
int atoi(const char *text);
long long atoll(const char *text);
long random(void);
double strtod(const char *text, char **endptr);
long strtol(const char *text, char **endptr, int base);
long long strtoll(const char *text, char **endptr, int base);
unsigned long strtoul(const char *text, char **endptr, int base);
unsigned long long strtoull(const char *text, char **endptr, int base);
int rand(void);
void srand(unsigned int seed);
void srandom_deterministic(unsigned int seed);
char *getenv(const char *name);
int system(const char *command);
void *reallocarray(void *ptr, size_t nmemb, size_t size);
void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *));
const char *getprogname(void);
void setprogname(const char *progname);
long long strtonum(const char *text, long long minval, long long maxval,
                   const char **errstrp);
uint32_t arc4random(void);
uint32_t arc4random_uniform(uint32_t upper_bound);
void arc4random_buf(void *buf, size_t size);

#endif
