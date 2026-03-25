#ifndef VIBE_TOOLS_BSD_HOST_COMPAT_H
#define VIBE_TOOLS_BSD_HOST_COMPAT_H

#include <stdint.h>
#include <sys/types.h>

#ifndef __dead
#define __dead __attribute__((noreturn))
#endif

int pledge(const char *promises, const char *execpromises);
int unveil(const char *path, const char *permissions);
void setprogname(const char *progname);
const char *getprogname(void);
size_t strlcpy(char *dst, const char *src, size_t size);
size_t strlcat(char *dst, const char *src, size_t size);
long long strtonum(const char *text, long long minval, long long maxval,
                   const char **errstrp);
uint32_t arc4random(void);
uint32_t arc4random_uniform(uint32_t upper_bound);
void *reallocarray(void *ptr, size_t nmemb, size_t size);

#endif
